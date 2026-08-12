#include "foundation/llvm_codegen.hpp"

#include "foundation/codegen.hpp"

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace foundation {

namespace {

struct LlvmModule {
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::TargetMachine> target;
};

struct EmittedValue {
    llvm::Value *value{};
    bool diverges{};
};

std::string safeName(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (const auto value : name) {
        const auto byte = static_cast<unsigned char>(value);
        result.push_back(std::isalnum(byte) != 0 ? value : '_');
    }
    return result;
}

std::string_view unqualifiedName(std::string_view name) {
    const auto separator = name.rfind('.');
    return name.substr(separator == std::string_view::npos ? 0 : separator + 1);
}

std::string traceFunctionName(const FirFunction &function) {
    if (function.testName.has_value()) {
        return "test \"" + *function.testName + '"';
    }
    if (!function.packageName.empty() &&
        function.name.starts_with(function.packageName + '.')) {
        return function.name.substr(function.packageName.size() + 1);
    }
    return std::string(unqualifiedName(function.name));
}

std::string functionName(const FirProgram &program, FirFunctionId id) {
    if (id == program.main) {
        return "fdn_program_main";
    }
    const auto &function = program.functions[id];
    auto name = "fdn_fn_" + safeName(function.name) + "_" +
                std::to_string(function.source);
    if (function.generic) {
        name += "_g" + std::to_string(id);
    }
    return name;
}

std::string integerTypeTag(Type type) {
    switch (type.kind) {
    case TypeKind::I8:
        return "i8";
    case TypeKind::I16:
        return "i16";
    case TypeKind::I32:
        return "i32";
    case TypeKind::I64:
        return "i64";
    case TypeKind::U8:
        return "u8";
    case TypeKind::U16:
        return "u16";
    case TypeKind::U32:
        return "u32";
    case TypeKind::U64:
        return "u64";
    case TypeKind::Isize:
        return "isize";
    case TypeKind::Usize:
        return "usize";
    default:
        return {};
    }
}

void initializeLlvmTargets() {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmPrinters();
    });
}

std::unique_ptr<llvm::TargetMachine> createTargetMachine(std::string triple,
                                                         Diagnostics &diagnostics) {
    initializeLlvmTargets();
    triple = llvm::Triple::normalize(triple);
    std::string error;
    const auto *target = llvm::TargetRegistry::lookupTarget(triple, error);
    if (target == nullptr) {
        diagnostics.error("FDN8002", "LLVM target lookup failed: " + error, {});
        return nullptr;
    }
    llvm::TargetOptions options;
    return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
        llvm::Triple(triple), "generic", "", options, llvm::Reloc::PIC_,
        std::nullopt, llvm::CodeGenOptLevel::Default));
}

class LlvmEmitter {
  public:
    LlvmEmitter(const FirProgram &source, std::string_view sourcePath,
                const LlvmCodegenOptions &options, Diagnostics &diagnostics,
                llvm::LLVMContext &context, llvm::Module &module)
        : program_(prepareFirForBackend(source, options.entry)),
          sourcePath_(sourcePath), options_(options), diagnostics_(diagnostics),
          context_(context), module_(module), builder_(context) {
        stringType_ = llvm::StructType::create(
            context_, {pointerType(), sizeType(), llvm::Type::getInt8Ty(context_)},
            "fdn.string");
        frameType_ = llvm::StructType::create(
            context_, {pointerType(), pointerType(), pointerType(), pointerType(),
                       llvm::Type::getInt32Ty(context_),
                       llvm::Type::getInt32Ty(context_),
                       llvm::Type::getInt8Ty(context_)},
            "fdn.frame");
    }

    bool run() {
        if (program_.main >= program_.functions.size()) {
            fail({}, "program has no LLVM entry point");
            return false;
        }
        declareFunctions();
        if (diagnostics_.hasErrors()) {
            return false;
        }
        for (FirFunctionId id = 0; id < program_.functions.size(); ++id) {
            if (program_.functions[id].hasBody) {
                emitFunction(id);
            }
        }
        if (!diagnostics_.hasErrors()) {
            emitMainWrapper();
        }
        return !diagnostics_.hasErrors();
    }

  private:
    struct LoopTarget {
        llvm::BasicBlock *breakBlock{};
        llvm::BasicBlock *continueBlock{};
    };

    llvm::PointerType *pointerType() const { return llvm::PointerType::get(context_, 0); }

    llvm::IntegerType *sizeType() const {
        const auto bits = module_.getDataLayout().getPointerSizeInBits();
        return llvm::IntegerType::get(context_, bits == 0 ? 64 : bits);
    }

    llvm::Type *typeOf(const Type &type) {
        switch (type.kind) {
        case TypeKind::Void:
        case TypeKind::Never:
            return llvm::Type::getVoidTy(context_);
        case TypeKind::I8:
        case TypeKind::U8:
            return llvm::Type::getInt8Ty(context_);
        case TypeKind::I16:
        case TypeKind::U16:
            return llvm::Type::getInt16Ty(context_);
        case TypeKind::I32:
        case TypeKind::U32:
            return llvm::Type::getInt32Ty(context_);
        case TypeKind::I64:
        case TypeKind::U64:
            return llvm::Type::getInt64Ty(context_);
        case TypeKind::Isize:
        case TypeKind::Usize:
            return sizeType();
        case TypeKind::F32:
            return llvm::Type::getFloatTy(context_);
        case TypeKind::F64:
            return llvm::Type::getDoubleTy(context_);
        case TypeKind::Bool:
            return llvm::Type::getInt1Ty(context_);
        case TypeKind::String:
            return stringType_;
        case TypeKind::Raw:
        case TypeKind::RawConst:
        case TypeKind::Own:
        case TypeKind::View:
        case TypeKind::Edit:
        case TypeKind::Task:
        case TypeKind::Sender:
        case TypeKind::Receiver:
            return pointerType();
        case TypeKind::Array:
            if (type.arguments.size() == 1) {
                if (auto *element = typeOf(type.arguments.front());
                    element != nullptr && !element->isVoidTy()) {
                    return llvm::ArrayType::get(element, type.declaration);
                }
            }
            return nullptr;
        case TypeKind::Invalid:
        case TypeKind::Slice:
        case TypeKind::Parameter:
        case TypeKind::Struct:
        case TypeKind::Enum:
        case TypeKind::Contract:
        case TypeKind::Function:
        case TypeKind::Channel:
            return nullptr;
        }
        return nullptr;
    }

    llvm::FunctionType *functionType(const FirFunction &function) {
        auto *result = function.diverges ? llvm::Type::getVoidTy(context_)
                                         : typeOf(function.returnType);
        if (result == nullptr) {
            fail(function.sourceSpan,
                 "LLVM backend does not support the return type of " + function.name);
            return nullptr;
        }
        std::vector<llvm::Type *> parameters;
        parameters.reserve(function.parameters.size());
        for (const auto local : function.parameters) {
            if (local >= function.locals.size()) {
                fail(function.sourceSpan, "LLVM backend received an invalid parameter");
                return nullptr;
            }
            auto *parameter = typeOf(function.locals[local].type);
            if (parameter == nullptr || parameter->isVoidTy()) {
                fail(function.sourceSpan,
                     "LLVM backend does not support a parameter type of " + function.name);
                return nullptr;
            }
            parameters.push_back(parameter);
        }
        if (function.closure || function.task || function.callback ||
            function.stateTransition.has_value() || function.stateTimeout.has_value() ||
            function.workflow.has_value()) {
            fail(function.sourceSpan,
                 "LLVM backend has not lowered the specialized function " + function.name);
            return nullptr;
        }
        return llvm::FunctionType::get(result, parameters, false);
    }

    void declareFunctions() {
        functions_.resize(program_.functions.size());
        for (FirFunctionId id = 0; id < program_.functions.size(); ++id) {
            const auto &function = program_.functions[id];
            auto *signature = functionType(function);
            if (signature == nullptr) {
                continue;
            }
            const auto symbol = !function.hasBody && function.cSymbol.has_value()
                                    ? *function.cSymbol
                                    : functionName(program_, id);
            auto *declaration = llvm::Function::Create(
                signature, llvm::GlobalValue::ExternalLinkage, symbol, module_);
            if (function.diverges) {
                declaration->addFnAttr(llvm::Attribute::NoReturn);
            }
            functions_[id] = declaration;
        }
    }

    void emitFunction(FirFunctionId id) {
        function_ = &program_.functions[id];
        functionId_ = id;
        llvmFunction_ = functions_[id];
        if (llvmFunction_ == nullptr || !llvmFunction_->empty()) {
            return;
        }
        auto *entry = llvm::BasicBlock::Create(context_, "entry", llvmFunction_);
        builder_.SetInsertPoint(entry);
        locals_.assign(function_->locals.size(), nullptr);
        for (FirLocalId local = 0; local < function_->locals.size(); ++local) {
            auto *type = typeOf(function_->locals[local].type);
            if (type == nullptr || type->isVoidTy()) {
                fail(function_->sourceSpan,
                     "LLVM backend does not support local " + function_->locals[local].name);
                continue;
            }
            locals_[local] = builder_.CreateAlloca(type, nullptr,
                                                   "local." + std::to_string(local));
            builder_.CreateStore(llvm::Constant::getNullValue(type), locals_[local]);
        }
        if (diagnostics_.hasErrors()) {
            return;
        }
        std::size_t parameterIndex{};
        for (auto &argument : llvmFunction_->args()) {
            const auto local = function_->parameters[parameterIndex++];
            builder_.CreateStore(&argument, locals_[local]);
        }
        frame_ = builder_.CreateAlloca(frameType_, nullptr, "frame");
        enterFrame();
        const auto exits = emitBlock(function_->body);
        if (diagnostics_.hasErrors()) {
            return;
        }
        if (!exits && builder_.GetInsertBlock()->getTerminator() == nullptr) {
            if (function_->returnType == voidType) {
                leaveFrame();
                builder_.CreateRetVoid();
            } else if (function_->diverges) {
                builder_.CreateUnreachable();
            } else {
                fail(function_->sourceSpan,
                     "LLVM backend reached the end of a value-returning function");
            }
        }
    }

    bool emitBlock(FirBlockId id) {
        if (id >= function_->blocks.size()) {
            fail(function_->sourceSpan, "LLVM backend received an invalid block");
            return true;
        }
        for (const auto statement : function_->blocks[id].statements) {
            if (emitStatement(statement)) {
                return true;
            }
        }
        dropLocals(function_->blocks[id].drops);
        return builder_.GetInsertBlock()->getTerminator() != nullptr;
    }

    bool emitStatement(FirStatementId id) {
        if (id >= function_->statements.size()) {
            fail(function_->sourceSpan, "LLVM backend received an invalid statement");
            return true;
        }
        const auto &statement = function_->statements[id];
        setLocation(statement.span);
        if (const auto *variable = std::get_if<FirVariableStatement>(&statement.value)) {
            const auto value = emitExpression(variable->initializer);
            if (!value.diverges && variable->local < locals_.size()) {
                builder_.CreateStore(value.value, locals_[variable->local]);
            }
        } else if (const auto *assignment =
                       std::get_if<FirAssignmentStatement>(&statement.value)) {
            const auto value = emitExpression(assignment->value);
            if (!value.diverges) {
                if (auto *address = emitAddress(assignment->target); address != nullptr) {
                    builder_.CreateStore(value.value, address);
                }
            }
        } else if (const auto *expression =
                       std::get_if<FirExpressionStatement>(&statement.value)) {
            emitExpression(expression->expression);
        } else if (const auto *discard =
                       std::get_if<FirDiscardStatement>(&statement.value)) {
            const auto value = emitExpression(discard->expression);
            if (!value.diverges) {
                dropValue(value.value, function_->expressions[discard->expression].type);
            }
        } else if (const auto *returned =
                       std::get_if<FirReturnStatement>(&statement.value)) {
            EmittedValue value;
            if (returned->value.has_value()) {
                value = emitExpression(*returned->value);
            }
            if (!value.diverges) {
                dropLocals(returned->drops);
                leaveFrame();
                if (returned->value.has_value()) {
                    builder_.CreateRet(value.value);
                } else {
                    builder_.CreateRetVoid();
                }
            }
        } else if (const auto *branch = std::get_if<FirIfStatement>(&statement.value)) {
            return emitIf(*branch);
        } else if (const auto *loop = std::get_if<FirWhileStatement>(&statement.value)) {
            emitWhile(*loop);
        } else if (const auto *broken =
                       std::get_if<FirBreakStatement>(&statement.value)) {
            dropLocals(broken->drops);
            if (loops_.empty()) {
                fail(statement.span, "LLVM backend received break outside a loop");
            } else {
                builder_.CreateBr(loops_.back().breakBlock);
            }
        } else if (const auto *continued =
                       std::get_if<FirContinueStatement>(&statement.value)) {
            dropLocals(continued->drops);
            if (loops_.empty()) {
                fail(statement.span, "LLVM backend received continue outside a loop");
            } else {
                builder_.CreateBr(loops_.back().continueBlock);
            }
        } else if (const auto *unsafe =
                       std::get_if<FirUnsafeStatement>(&statement.value)) {
            return emitBlock(unsafe->body);
        } else {
            fail(statement.span, "LLVM backend has not lowered this statement yet");
        }
        return builder_.GetInsertBlock()->getTerminator() != nullptr;
    }

    bool emitIf(const FirIfStatement &branch) {
        const auto condition = emitExpression(branch.condition);
        if (condition.diverges) {
            return true;
        }
        auto *thenBlock = llvm::BasicBlock::Create(context_, "if.then", llvmFunction_);
        auto *elseBlock = branch.elseBlock.has_value()
                              ? llvm::BasicBlock::Create(context_, "if.else", llvmFunction_)
                              : nullptr;
        auto *mergeBlock = llvm::BasicBlock::Create(context_, "if.end", llvmFunction_);
        builder_.CreateCondBr(condition.value, thenBlock,
                              elseBlock == nullptr ? mergeBlock : elseBlock);

        builder_.SetInsertPoint(thenBlock);
        const auto thenExits = emitBlock(branch.thenBlock);
        if (!thenExits) {
            builder_.CreateBr(mergeBlock);
        }
        auto elseExits = false;
        if (elseBlock != nullptr) {
            builder_.SetInsertPoint(elseBlock);
            elseExits = emitBlock(*branch.elseBlock);
            if (!elseExits) {
                builder_.CreateBr(mergeBlock);
            }
        }
        if (thenExits && elseBlock != nullptr && elseExits) {
            mergeBlock->eraseFromParent();
            return true;
        }
        builder_.SetInsertPoint(mergeBlock);
        return false;
    }

    void emitWhile(const FirWhileStatement &loop) {
        auto *conditionBlock = llvm::BasicBlock::Create(context_, "while.condition", llvmFunction_);
        auto *bodyBlock = llvm::BasicBlock::Create(context_, "while.body", llvmFunction_);
        auto *exitBlock = llvm::BasicBlock::Create(context_, "while.end", llvmFunction_);
        builder_.CreateBr(conditionBlock);
        builder_.SetInsertPoint(conditionBlock);
        const auto condition = emitExpression(loop.condition);
        if (condition.diverges) {
            exitBlock->eraseFromParent();
            bodyBlock->eraseFromParent();
            return;
        }
        builder_.CreateCondBr(condition.value, bodyBlock, exitBlock);
        builder_.SetInsertPoint(bodyBlock);
        loops_.push_back({exitBlock, conditionBlock});
        const auto exits = emitBlock(loop.body);
        loops_.pop_back();
        if (!exits) {
            builder_.CreateBr(conditionBlock);
        }
        builder_.SetInsertPoint(exitBlock);
    }

    EmittedValue emitExpression(FirExpressionId id) {
        if (id >= function_->expressions.size()) {
            fail(function_->sourceSpan, "LLVM backend received an invalid expression");
            return {};
        }
        const auto &expression = function_->expressions[id];
        setLocation(expression.span);
        if (const auto *integer =
                std::get_if<FirIntegerExpression>(&expression.value)) {
            auto *type = llvm::dyn_cast_or_null<llvm::IntegerType>(typeOf(expression.type));
            if (type == nullptr) {
                fail(expression.span, "LLVM integer literal has a non-integer type");
                return {};
            }
            llvm::APInt value(type->getBitWidth(), integer->magnitude, false);
            if (integer->negative) {
                value = -value;
            }
            return {llvm::ConstantInt::get(type, value), false};
        }
        if (const auto *floating =
                std::get_if<FirFloatingExpression>(&expression.value)) {
            auto *type = typeOf(expression.type);
            return {llvm::ConstantFP::get(type, floating->text), false};
        }
        if (const auto *boolean =
                std::get_if<FirBooleanExpression>(&expression.value)) {
            return {llvm::ConstantInt::getBool(context_, boolean->value), false};
        }
        if (const auto *string = std::get_if<FirStringExpression>(&expression.value)) {
            auto *data = builder_.CreateGlobalString(string->value, "string");
            llvm::Value *value = llvm::PoisonValue::get(stringType_);
            value = builder_.CreateInsertValue(value, data, 0);
            value = builder_.CreateInsertValue(
                value, llvm::ConstantInt::get(sizeType(), string->value.size()), 1);
            value = builder_.CreateInsertValue(
                value, llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), 0), 2);
            return {value, false};
        }
        if (const auto *local = std::get_if<FirLocalExpression>(&expression.value)) {
            return {loadLocal(local->local), false};
        }
        if (const auto *read = std::get_if<FirReadExpression>(&expression.value)) {
            auto *address = loadLocal(read->local);
            auto *target = typeOf(expression.type);
            if (address == nullptr || target == nullptr) {
                return {};
            }
            return {builder_.CreateLoad(target, address, "read"), false};
        }
        if (const auto *moved = std::get_if<FirMoveExpression>(&expression.value)) {
            auto *value = loadLocal(moved->local);
            if (value != nullptr && moved->local < locals_.size()) {
                builder_.CreateStore(llvm::Constant::getNullValue(value->getType()),
                                     locals_[moved->local]);
            }
            return {value, false};
        }
        if (const auto *unary = std::get_if<FirUnaryExpression>(&expression.value)) {
            return emitUnary(*unary, expression.type, expression.span);
        }
        if (const auto *binary = std::get_if<FirBinaryExpression>(&expression.value)) {
            return emitBinary(*binary, expression.type, expression.span);
        }
        if (const auto *call = std::get_if<FirCallExpression>(&expression.value)) {
            return emitCall(*call, expression.type, expression.span);
        }
        if (const auto *conditional =
                std::get_if<FirConditionalExpression>(&expression.value)) {
            return emitConditional(*conditional, expression.type);
        }
        fail(expression.span, "LLVM backend has not lowered this expression yet");
        return {};
    }

    EmittedValue emitUnary(const FirUnaryExpression &unary, const Type &type,
                           SourceSpan span) {
        const auto operand = emitExpression(unary.operand);
        if (operand.diverges) {
            return operand;
        }
        switch (unary.operation) {
        case FirUnaryOperator::Negate:
            if (isFloating(type)) {
                return {builder_.CreateFNeg(operand.value), false};
            }
            if (isSignedInteger(type)) {
                const auto tag = integerTypeTag(type);
                auto callee = runtimeFunction("fdn_" + tag + "_negate", typeOf(type),
                                              {typeOf(type)});
                setLocation(span);
                return {builder_.CreateCall(callee, {operand.value}), false};
            }
            break;
        case FirUnaryOperator::Not:
            return {builder_.CreateNot(operand.value), false};
        case FirUnaryOperator::Empty:
            if (type == boolType &&
                function_->expressions[unary.operand].type == stringType) {
                auto *length = builder_.CreateExtractValue(operand.value, 1);
                return {builder_.CreateICmpEQ(
                            length, llvm::ConstantInt::get(sizeType(), 0)),
                        false};
            }
            break;
        case FirUnaryOperator::Dereference:
            if (auto *target = typeOf(type); target != nullptr) {
                return {builder_.CreateLoad(target, operand.value), false};
            }
            break;
        }
        fail(span, "LLVM backend has not lowered this unary operation yet");
        return {};
    }

    EmittedValue emitBinary(const FirBinaryExpression &binary, const Type &type,
                            SourceSpan span) {
        if (binary.operation == FirBinaryOperator::And ||
            binary.operation == FirBinaryOperator::Or) {
            return emitLogical(binary);
        }
        const auto left = emitExpression(binary.left);
        if (left.diverges) {
            return left;
        }
        const auto right = emitExpression(binary.right);
        if (right.diverges) {
            return right;
        }
        switch (binary.operation) {
        case FirBinaryOperator::Add:
        case FirBinaryOperator::Subtract:
        case FirBinaryOperator::Multiply:
        case FirBinaryOperator::Divide:
        case FirBinaryOperator::Remainder:
            if (type == stringType && binary.operation == FirBinaryOperator::Add) {
                auto *result = builder_.CreateAlloca(stringType_, nullptr,
                                                     "string.concat.result");
                builder_.CreateCall(
                    runtimeFunction("fdn_abi_string_concat",
                                    llvm::Type::getVoidTy(context_),
                                    {pointerType(), pointerType(), pointerType()}),
                    {result, stringAddress(left.value), stringAddress(right.value)});
                return {builder_.CreateLoad(stringType_, result), false};
            }
            if (isFloating(type)) {
                switch (binary.operation) {
                case FirBinaryOperator::Add:
                    return {builder_.CreateFAdd(left.value, right.value), false};
                case FirBinaryOperator::Subtract:
                    return {builder_.CreateFSub(left.value, right.value), false};
                case FirBinaryOperator::Multiply:
                    return {builder_.CreateFMul(left.value, right.value), false};
                case FirBinaryOperator::Divide:
                    return {builder_.CreateFDiv(left.value, right.value), false};
                case FirBinaryOperator::Remainder:
                    return {builder_.CreateFRem(left.value, right.value), false};
                default:
                    break;
                }
            }
            if (isInteger(type)) {
                std::string operation;
                switch (binary.operation) {
                case FirBinaryOperator::Add:
                    operation = "add";
                    break;
                case FirBinaryOperator::Subtract:
                    operation = "subtract";
                    break;
                case FirBinaryOperator::Multiply:
                    operation = "multiply";
                    break;
                case FirBinaryOperator::Divide:
                    operation = "divide";
                    break;
                case FirBinaryOperator::Remainder:
                    operation = "remainder";
                    break;
                default:
                    break;
                }
                setLocation(span);
                auto callee = runtimeFunction(
                    "fdn_" + integerTypeTag(type) + "_" + operation, typeOf(type),
                    {typeOf(type), typeOf(type)});
                return {builder_.CreateCall(callee, {left.value, right.value}), false};
            }
            break;
        case FirBinaryOperator::Equal:
        case FirBinaryOperator::NotEqual:
            if (function_->expressions[binary.left].type == stringType) {
                auto callee = runtimeFunction(
                    "fdn_abi_string_equal", llvm::Type::getInt32Ty(context_),
                    {pointerType(), pointerType()});
                auto *equal = builder_.CreateICmpNE(
                    builder_.CreateCall(
                        callee,
                        {stringAddress(left.value), stringAddress(right.value)}),
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0));
                return {binary.operation == FirBinaryOperator::Equal
                            ? equal
                            : builder_.CreateNot(equal),
                        false};
            }
            if (left.value->getType()->isFloatingPointTy()) {
                auto *comparison = binary.operation == FirBinaryOperator::Equal
                                       ? builder_.CreateFCmpOEQ(left.value, right.value)
                                       : builder_.CreateFCmpUNE(left.value, right.value);
                return {comparison, false};
            }
            return {binary.operation == FirBinaryOperator::Equal
                        ? builder_.CreateICmpEQ(left.value, right.value)
                        : builder_.CreateICmpNE(left.value, right.value),
                    false};
        case FirBinaryOperator::Less:
        case FirBinaryOperator::LessEqual:
        case FirBinaryOperator::Greater:
        case FirBinaryOperator::GreaterEqual:
            return emitComparison(binary.operation, left.value, right.value,
                                  function_->expressions[binary.left].type);
        case FirBinaryOperator::And:
        case FirBinaryOperator::Or:
            break;
        }
        fail(span, "LLVM backend has not lowered this binary operation yet");
        return {};
    }

    EmittedValue emitLogical(const FirBinaryExpression &binary) {
        const auto left = emitExpression(binary.left);
        if (left.diverges) {
            return left;
        }
        auto *origin = builder_.GetInsertBlock();
        auto *rightBlock = llvm::BasicBlock::Create(context_, "logic.right", llvmFunction_);
        auto *mergeBlock = llvm::BasicBlock::Create(context_, "logic.end", llvmFunction_);
        if (binary.operation == FirBinaryOperator::And) {
            builder_.CreateCondBr(left.value, rightBlock, mergeBlock);
        } else {
            builder_.CreateCondBr(left.value, mergeBlock, rightBlock);
        }
        builder_.SetInsertPoint(rightBlock);
        const auto right = emitExpression(binary.right);
        if (right.diverges) {
            builder_.SetInsertPoint(mergeBlock);
            auto *result = builder_.CreatePHI(llvm::Type::getInt1Ty(context_), 1);
            result->addIncoming(left.value, origin);
            return {result, false};
        }
        builder_.CreateBr(mergeBlock);
        rightBlock = builder_.GetInsertBlock();
        builder_.SetInsertPoint(mergeBlock);
        auto *result = builder_.CreatePHI(llvm::Type::getInt1Ty(context_), 2);
        result->addIncoming(left.value, origin);
        result->addIncoming(right.value, rightBlock);
        return {result, false};
    }

    EmittedValue emitComparison(FirBinaryOperator operation, llvm::Value *left,
                                llvm::Value *right, Type type) {
        if (left->getType()->isFloatingPointTy()) {
            switch (operation) {
            case FirBinaryOperator::Less:
                return {builder_.CreateFCmpOLT(left, right), false};
            case FirBinaryOperator::LessEqual:
                return {builder_.CreateFCmpOLE(left, right), false};
            case FirBinaryOperator::Greater:
                return {builder_.CreateFCmpOGT(left, right), false};
            case FirBinaryOperator::GreaterEqual:
                return {builder_.CreateFCmpOGE(left, right), false};
            default:
                break;
            }
        }
        const auto signedComparison = isSignedInteger(type);
        switch (operation) {
        case FirBinaryOperator::Less:
            return {signedComparison ? builder_.CreateICmpSLT(left, right)
                                     : builder_.CreateICmpULT(left, right),
                    false};
        case FirBinaryOperator::LessEqual:
            return {signedComparison ? builder_.CreateICmpSLE(left, right)
                                     : builder_.CreateICmpULE(left, right),
                    false};
        case FirBinaryOperator::Greater:
            return {signedComparison ? builder_.CreateICmpSGT(left, right)
                                     : builder_.CreateICmpUGT(left, right),
                    false};
        case FirBinaryOperator::GreaterEqual:
            return {signedComparison ? builder_.CreateICmpSGE(left, right)
                                     : builder_.CreateICmpUGE(left, right),
                    false};
        default:
            break;
        }
        return {};
    }

    EmittedValue emitCall(const FirCallExpression &call, const Type &type,
                          SourceSpan span) {
        std::vector<EmittedValue> arguments;
        arguments.reserve(call.arguments.size());
        for (const auto argument : call.arguments) {
            arguments.push_back(emitExpression(argument));
            if (arguments.back().diverges) {
                return arguments.back();
            }
        }
        std::vector<llvm::Value *> values;
        values.reserve(arguments.size());
        std::transform(arguments.begin(), arguments.end(), std::back_inserter(values),
                       [](const auto &argument) { return argument.value; });
        setLocation(span);
        llvm::Value *result{};
        switch (call.kind) {
        case FirCallKind::Function:
            if (call.function >= functions_.size() || functions_[call.function] == nullptr) {
                fail(span, "LLVM backend received an invalid function call");
                return {};
            }
            result = builder_.CreateCall(functions_[call.function], values);
            break;
        case FirCallKind::Print:
            if (values.size() != 1) {
                fail(span, "LLVM print call has invalid arguments");
                return {};
            }
            result = builder_.CreateCall(
                runtimeFunction("fdn_abi_println", llvm::Type::getVoidTy(context_),
                                {pointerType()}),
                {stringAddress(values.front())});
            break;
        case FirCallKind::Panic:
            if (values.size() != 1) {
                fail(span, "LLVM panic call has invalid arguments");
                return {};
            }
            builder_.CreateCall(
                runtimeFunction("fdn_abi_panic", llvm::Type::getVoidTy(context_),
                                {pointerType()}),
                {stringAddress(values.front())});
            builder_.CreateUnreachable();
            return {llvm::PoisonValue::get(llvm::Type::getInt8Ty(context_)), true};
        case FirCallKind::Len:
            if (values.size() == 1 &&
                function_->expressions[call.arguments.front()].type == stringType) {
                result = builder_.CreateExtractValue(values.front(), 1);
                break;
            }
            fail(span, "LLVM len call does not support this value yet");
            return {};
        case FirCallKind::Null:
            result = llvm::ConstantPointerNull::get(pointerType());
            break;
        case FirCallKind::IsNull:
            if (values.size() != 1) {
                fail(span, "LLVM isNull call has invalid arguments");
                return {};
            }
            result = builder_.CreateICmpEQ(
                values.front(), llvm::ConstantPointerNull::get(pointerType()));
            break;
        case FirCallKind::NumericConversion:
            if (values.size() != 1) {
                fail(span, "LLVM numeric conversion has invalid arguments");
                return {};
            }
            result = emitNumericConversion(values.front(),
                                           function_->expressions[call.arguments.front()].type,
                                           type, span);
            if (result == nullptr) {
                return {};
            }
            break;
        case FirCallKind::FunctionValue:
        case FirCallKind::Contract:
            fail(span, "LLVM backend has not lowered this call kind yet");
            return {};
        }
        for (std::size_t index = 0;
             index < values.size() && index < call.argumentDrops.size(); ++index) {
            if (call.argumentDrops[index]) {
                dropValue(values[index],
                          function_->expressions[call.arguments[index]].type);
            }
        }
        return {result, false};
    }

    llvm::Value *emitNumericConversion(llvm::Value *value, Type source, Type target,
                                       SourceSpan span) {
        auto *targetType = typeOf(target);
        if (targetType == nullptr) {
            fail(span, "LLVM numeric conversion has an unsupported target type");
            return nullptr;
        }
        if (isInteger(source) && isInteger(target)) {
            return builder_.CreateIntCast(value, targetType, isSignedInteger(source));
        }
        if (isInteger(source) && isFloating(target)) {
            return isSignedInteger(source) ? builder_.CreateSIToFP(value, targetType)
                                           : builder_.CreateUIToFP(value, targetType);
        }
        if (isFloating(source) && isInteger(target)) {
            return isSignedInteger(target) ? builder_.CreateFPToSI(value, targetType)
                                           : builder_.CreateFPToUI(value, targetType);
        }
        if (isFloating(source) && isFloating(target)) {
            return builder_.CreateFPCast(value, targetType);
        }
        fail(span, "LLVM numeric conversion received non-numeric types");
        return nullptr;
    }

    EmittedValue emitConditional(const FirConditionalExpression &conditional,
                                 const Type &type) {
        const auto condition = emitExpression(conditional.condition);
        if (condition.diverges) {
            return condition;
        }
        auto *thenBlock = llvm::BasicBlock::Create(context_, "value.then", llvmFunction_);
        auto *elseBlock = llvm::BasicBlock::Create(context_, "value.else", llvmFunction_);
        auto *mergeBlock = llvm::BasicBlock::Create(context_, "value.end", llvmFunction_);
        builder_.CreateCondBr(condition.value, thenBlock, elseBlock);

        std::vector<std::pair<llvm::Value *, llvm::BasicBlock *>> incoming;
        builder_.SetInsertPoint(thenBlock);
        if (!emitBlock(conditional.thenBlock)) {
            const auto thenValue = emitExpression(conditional.thenValue);
            if (!thenValue.diverges) {
                builder_.CreateBr(mergeBlock);
                incoming.emplace_back(thenValue.value, builder_.GetInsertBlock());
            }
        }

        builder_.SetInsertPoint(elseBlock);
        if (!emitBlock(conditional.elseBlock)) {
            const auto elseValue = emitExpression(conditional.elseValue);
            if (!elseValue.diverges) {
                builder_.CreateBr(mergeBlock);
                incoming.emplace_back(elseValue.value, builder_.GetInsertBlock());
            }
        }

        if (incoming.empty()) {
            mergeBlock->eraseFromParent();
            return {llvm::PoisonValue::get(llvm::Type::getInt8Ty(context_)), true};
        }

        builder_.SetInsertPoint(mergeBlock);
        if (incoming.size() == 1) {
            return {incoming.front().first, false};
        }
        auto *valueType = typeOf(type);
        auto *result = builder_.CreatePHI(valueType, 2);
        for (const auto &[value, block] : incoming) {
            result->addIncoming(value, block);
        }
        return {result, false};
    }

    llvm::Value *loadLocal(FirLocalId local) {
        if (local >= locals_.size() || locals_[local] == nullptr) {
            fail(function_->sourceSpan, "LLVM backend received an invalid local");
            return nullptr;
        }
        return builder_.CreateLoad(locals_[local]->getAllocatedType(), locals_[local],
                                   "local.value");
    }

    llvm::Value *emitAddress(FirExpressionId id) {
        if (id >= function_->expressions.size()) {
            return nullptr;
        }
        if (const auto *local =
                std::get_if<FirLocalExpression>(&function_->expressions[id].value)) {
            return local->local < locals_.size() ? locals_[local->local] : nullptr;
        }
        fail(function_->expressions[id].span,
             "LLVM backend has not lowered this assignment target yet");
        return nullptr;
    }

    llvm::FunctionCallee runtimeFunction(std::string_view name, llvm::Type *result,
                                         std::vector<llvm::Type *> parameters) {
        return module_.getOrInsertFunction(
            llvm::StringRef(name.data(), name.size()),
            llvm::FunctionType::get(result, parameters, false));
    }

    llvm::Value *stringAddress(llvm::Value *value) {
        auto *storage = builder_.CreateAlloca(stringType_, nullptr, "string.abi");
        builder_.CreateStore(value, storage);
        return storage;
    }

    void enterFrame() {
        const auto source = function_->sourcePath.empty()
                                ? sourcePath_
                                : std::string_view(function_->sourcePath);
        const auto package = function_->packageName.empty()
                                 ? std::string_view("main")
                                 : std::string_view(function_->packageName);
        auto *packageName = builder_.CreateGlobalString(package, "frame.package");
        auto *functionNameValue =
            builder_.CreateGlobalString(traceFunctionName(*function_), "frame.function");
        auto *sourceName = builder_.CreateGlobalString(source, "frame.source");
        builder_.CreateCall(
            runtimeFunction("fdn_frame_enter", llvm::Type::getVoidTy(context_),
                            {pointerType(), pointerType(), pointerType(), pointerType(),
                             llvm::Type::getInt32Ty(context_),
                             llvm::Type::getInt32Ty(context_)}),
            {frame_, packageName, functionNameValue, sourceName,
             llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                    function_->sourceSpan.line),
             llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                    function_->sourceSpan.column)});
    }

    void leaveFrame() {
        builder_.CreateCall(
            runtimeFunction("fdn_frame_leave", llvm::Type::getVoidTy(context_),
                            {pointerType()}),
            {frame_});
    }

    void setLocation(SourceSpan span) {
        if (frame_ == nullptr || builder_.GetInsertBlock() == nullptr ||
            builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        auto *line = builder_.CreateStructGEP(frameType_, frame_, 4);
        auto *column = builder_.CreateStructGEP(frameType_, frame_, 5);
        builder_.CreateStore(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), span.line), line);
        builder_.CreateStore(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), span.column), column);
    }

    void dropValue(llvm::Value *value, const Type &type) {
        if (value == nullptr || builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        if (type == stringType) {
            auto *storage = builder_.CreateAlloca(stringType_, nullptr, "drop.string");
            builder_.CreateStore(value, storage);
            builder_.CreateCall(
                runtimeFunction("fdn_string_drop", llvm::Type::getVoidTy(context_),
                                {pointerType()}),
                {storage});
        }
    }

    void dropLocals(const std::vector<FirLocalId> &locals) {
        for (const auto local : locals) {
            if (local >= function_->locals.size() || local >= locals_.size()) {
                continue;
            }
            dropValue(loadLocal(local), function_->locals[local].type);
        }
    }

    void emitMainWrapper() {
        const auto &entry = program_.functions[program_.main];
        if (!entry.parameters.empty()) {
            fail(entry.sourceSpan,
                 "LLVM backend has not lowered command-line arguments yet");
            return;
        }
        auto *signature = llvm::FunctionType::get(
            llvm::Type::getInt32Ty(context_), {}, false);
        auto *main = llvm::Function::Create(signature,
                                            llvm::GlobalValue::ExternalLinkage,
                                            "main", module_);
        auto *block = llvm::BasicBlock::Create(context_, "entry", main);
        builder_.SetInsertPoint(block);
        auto *entryFunction = functions_[program_.main];
        if (options_.entry.has_value()) {
            builder_.CreateCall(entryFunction);
            if (options_.verifyAllocations) {
                emitAllocationCheck();
            }
            builder_.CreateRet(
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0));
            return;
        }
        auto *result = builder_.CreateCall(entryFunction);
        if (options_.verifyAllocations) {
            emitAllocationCheck();
        }
        builder_.CreateRet(result);
    }

    void emitAllocationCheck() {
        auto *count = builder_.CreateCall(runtimeFunction(
            "fdn_live_allocations", sizeType(), {}));
        auto *clean = llvm::BasicBlock::Create(context_, "allocations.clean", mainFunction());
        auto *failed = llvm::BasicBlock::Create(context_, "allocations.failed", mainFunction());
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(count, llvm::ConstantInt::get(sizeType(), 0)),
            clean, failed);
        builder_.SetInsertPoint(failed);
        auto *message = builder_.CreateGlobalString(
            "live allocations after entry point", "allocations.message");
        builder_.CreateCall(
            runtimeFunction("fdn_panic_cstr", llvm::Type::getVoidTy(context_),
                            {pointerType()}),
            {message});
        builder_.CreateUnreachable();
        builder_.SetInsertPoint(clean);
    }

    llvm::Function *mainFunction() const { return builder_.GetInsertBlock()->getParent(); }

    void fail(SourceSpan span, std::string message) {
        diagnostics_.error("FDN8001", std::move(message), span);
    }

    FirProgram program_;
    std::string sourcePath_;
    const LlvmCodegenOptions &options_;
    Diagnostics &diagnostics_;
    llvm::LLVMContext &context_;
    llvm::Module &module_;
    llvm::IRBuilder<> builder_;
    llvm::StructType *stringType_{};
    llvm::StructType *frameType_{};
    std::vector<llvm::Function *> functions_;
    const FirFunction *function_{};
    FirFunctionId functionId_{};
    llvm::Function *llvmFunction_{};
    llvm::AllocaInst *frame_{};
    std::vector<llvm::AllocaInst *> locals_;
    std::vector<LoopTarget> loops_;
};

std::optional<LlvmModule> buildModule(const FirProgram &program,
                                      std::string_view sourcePath,
                                      const LlvmCodegenOptions &options,
                                      Diagnostics &diagnostics) {
    const auto triple = options.targetTriple.empty()
                            ? defaultLlvmTargetTriple()
                            : llvm::Triple::normalize(options.targetTriple);
    auto target = createTargetMachine(triple, diagnostics);
    if (target == nullptr) {
        return std::nullopt;
    }
    LlvmModule result;
    result.context = std::make_unique<llvm::LLVMContext>();
    result.module = std::make_unique<llvm::Module>("foundation.program", *result.context);
    result.target = std::move(target);
    result.module->setTargetTriple(llvm::Triple(triple));
    result.module->setDataLayout(result.target->createDataLayout());
    LlvmEmitter emitter(program, sourcePath, options, diagnostics, *result.context,
                        *result.module);
    if (!emitter.run()) {
        return std::nullopt;
    }
    std::string verification;
    llvm::raw_string_ostream verificationOutput(verification);
    if (llvm::verifyModule(*result.module, &verificationOutput)) {
        verificationOutput.flush();
        diagnostics.error("FDN8003", "invalid LLVM module: " + verification, {});
        return std::nullopt;
    }
    if (options.optimize) {
        llvm::LoopAnalysisManager loops;
        llvm::FunctionAnalysisManager functions;
        llvm::CGSCCAnalysisManager cgscc;
        llvm::ModuleAnalysisManager modules;
        llvm::PassBuilder passes(result.target.get());
        passes.registerModuleAnalyses(modules);
        passes.registerCGSCCAnalyses(cgscc);
        passes.registerFunctionAnalyses(functions);
        passes.registerLoopAnalyses(loops);
        passes.crossRegisterProxies(loops, functions, cgscc, modules);
        auto pipeline =
            passes.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
        pipeline.run(*result.module, modules);
    }
    return result;
}

} // namespace

std::string defaultLlvmTargetTriple() {
    return llvm::Triple::normalize(llvm::sys::getDefaultTargetTriple());
}

std::optional<std::string> emitLlvmIr(const FirProgram &program,
                                      std::string_view sourcePath,
                                      const LlvmCodegenOptions &options,
                                      Diagnostics &diagnostics) {
    auto generated = buildModule(program, sourcePath, options, diagnostics);
    if (!generated.has_value()) {
        return std::nullopt;
    }
    std::string result;
    llvm::raw_string_ostream output(result);
    generated->module->print(output, nullptr);
    output.flush();
    return result;
}

bool emitLlvmObject(const FirProgram &program, const std::filesystem::path &output,
                    std::string_view sourcePath,
                    const LlvmCodegenOptions &options,
                    Diagnostics &diagnostics) {
    auto generated = buildModule(program, sourcePath, options, diagnostics);
    if (!generated.has_value()) {
        return false;
    }
    std::error_code error;
    llvm::raw_fd_ostream object(output.string(), error, llvm::sys::fs::OF_None);
    if (error) {
        diagnostics.error("FDN8004", "cannot create LLVM object: " + error.message(), {});
        return false;
    }
    llvm::legacy::PassManager passes;
    if (generated->target->addPassesToEmitFile(
            passes, object, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        diagnostics.error("FDN8004", "LLVM target cannot emit object files", {});
        return false;
    }
    passes.run(*generated->module);
    object.flush();
    if (object.has_error()) {
        diagnostics.error("FDN8004", "LLVM failed while writing the object file", {});
        return false;
    }
    return true;
}

} // namespace foundation
