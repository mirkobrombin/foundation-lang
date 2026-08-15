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
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
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
    if (!function.packageName.empty() && function.name.starts_with(function.packageName + '.')) {
        return function.name.substr(function.packageName.size() + 1);
    }
    return std::string(unqualifiedName(function.name));
}

std::string functionName(const FirProgram &program, FirFunctionId id) {
    if (id == program.main) {
        return "fdn_program_main";
    }
    const auto &function = program.functions[id];
    auto name = "fdn_fn_" + safeName(function.name) + "_" + std::to_string(function.source);
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

std::string llvmTypeKey(const Type &type) {
    std::string result =
        std::to_string(static_cast<int>(type.kind)) + ':' + std::to_string(type.declaration);
    for (const auto &argument : type.arguments) {
        result += '[' + llvmTypeKey(argument) + ']';
    }
    return result;
}

std::string functionAdapterName(const FirProgram &program, FirFunctionId id) {
    return functionName(program, id) + "_value_adapter";
}

std::string closureEnvironmentName(const FirProgram &program, FirFunctionId id) {
    return functionName(program, id) + "_environment";
}

std::string closureDropName(const FirProgram &program, FirFunctionId id) {
    return closureEnvironmentName(program, id) + "_drop";
}

std::string vtableName(const Type &contract, const Type &concrete) {
    return "fdn_vtable_c" + std::to_string(contract.declaration) + "_s" +
           std::to_string(concrete.declaration);
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
    return std::unique_ptr<llvm::TargetMachine>(
        target->createTargetMachine(llvm::Triple(triple), "generic", "", options, llvm::Reloc::PIC_,
                                    std::nullopt, llvm::CodeGenOptLevel::Default));
}

class LlvmEmitter {
  public:
    LlvmEmitter(const FirProgram &source, std::string_view sourcePath,
                const LlvmCodegenOptions &options, Diagnostics &diagnostics,
                llvm::LLVMContext &context, llvm::Module &module)
        : program_(prepareFirForBackend(source, options.entry)), sourcePath_(sourcePath),
          options_(options), diagnostics_(diagnostics), context_(context), module_(module),
          builder_(context) {
        stringType_ = llvm::StructType::create(
            context_, {pointerType(), sizeType(), llvm::Type::getInt8Ty(context_)},
            "fdn.string");
        frameType_ = llvm::StructType::create(
            context_, {pointerType(), pointerType(), pointerType(), pointerType(),
                       llvm::Type::getInt32Ty(context_),
                       llvm::Type::getInt32Ty(context_),
                       llvm::Type::getInt8Ty(context_)},
            "fdn.frame");
        functionValueType_ = llvm::StructType::create(
            context_, {pointerType(), pointerType(), pointerType()}, "fdn.function");
        channelType_ =
            llvm::StructType::create(context_, {pointerType(), pointerType()}, "fdn.channel");
    }

    bool run() {
        if (program_.main >= program_.functions.size()) {
            fail({}, "program has no LLVM entry point");
            return false;
        }
        prepareTypes();
        if (diagnostics_.hasErrors()) {
            return false;
        }
        declareFunctions();
        if (diagnostics_.hasErrors()) {
            return false;
        }
        declareCallableSupport();
        emitCallableSupport();
        for (FirFunctionId id = 0; id < program_.functions.size(); ++id) {
            if (program_.functions[id].hasBody) {
                emitFunction(id);
            }
        }
        emitNativeWrappers();
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

    struct ContractSupport {
        Type contract{invalidType};
        Type concrete{invalidType};
        std::vector<FirContractMethodTarget> targets;
        llvm::Function *drop{};
        std::vector<llvm::Function *> methods;
        llvm::GlobalVariable *vtable{};
    };

    llvm::PointerType *pointerType() const { return llvm::PointerType::get(context_, 0); }

    llvm::IntegerType *sizeType() const {
        const auto bits = module_.getDataLayout().getPointerSizeInBits();
        return llvm::IntegerType::get(context_, bits == 0 ? 64 : bits);
    }

    void prepareTypes() {
        structTypes_.resize(program_.structs.size());
        for (FirStructId id = 0; id < program_.structs.size(); ++id) {
            structTypes_[id] =
                llvm::StructType::create(context_, "fdn.struct." + std::to_string(id));
        }
        enumTypes_.resize(program_.enums.size());
        for (FirEnumId id = 0; id < program_.enums.size(); ++id) {
            enumTypes_[id] =
                llvm::StructType::create(context_, "fdn.enum." + std::to_string(id));
        }
        contractTypes_.resize(program_.contracts.size());
        contractVtableTypes_.resize(program_.contracts.size());
        for (FirContractId id = 0; id < program_.contracts.size(); ++id) {
            contractTypes_[id] = llvm::StructType::create(context_, {pointerType(), pointerType()},
                                                          "fdn.contract." + std::to_string(id));
            std::vector<llvm::Type *> entries(program_.contracts[id].methods.size() + 1,
                                              pointerType());
            contractVtableTypes_[id] = llvm::StructType::create(
                context_, entries, "fdn.contract." + std::to_string(id) + ".vtable");
        }
        for (FirStructId id = 0; id < program_.structs.size(); ++id) {
            layOutStruct(id);
        }
        for (FirEnumId id = 0; id < program_.enums.size(); ++id) {
            const auto &declaration = program_.enums[id];
            std::vector<llvm::Type *> fields{llvm::Type::getInt32Ty(context_)};
            fields.reserve(declaration.variants.size() + 1);
            for (const auto &variant : declaration.variants) {
                auto *payload = variant.payload.has_value()
                                    ? typeOf(*variant.payload)
                                    : static_cast<llvm::Type *>(
                                          llvm::Type::getInt8Ty(context_));
                if (payload == nullptr || payload->isVoidTy()) {
                    fail({}, "LLVM backend cannot lay out variant " + variant.name +
                                 " of " + declaration.name);
                    payload = llvm::Type::getInt8Ty(context_);
                }
                fields.push_back(payload);
            }
            enumTypes_[id]->setBody(fields);
        }
        for (FirStructId id = 0; id < program_.structs.size(); ++id) {
            if (!structTypes_[id]->isSized()) {
                fail(program_.structs[id].sourceSpan,
                     "LLVM backend cannot resolve the layout of " + program_.structs[id].name);
            }
        }
        for (FirEnumId id = 0; id < program_.enums.size(); ++id) {
            if (!enumTypes_[id]->isSized()) {
                fail({}, "LLVM backend cannot resolve the layout of " +
                             program_.enums[id].name);
            }
        }
        closureEnvironmentTypes_.resize(program_.functions.size());
        for (FirFunctionId id = 0; id < program_.functions.size(); ++id) {
            const auto &function = program_.functions[id];
            if (!function.closure) {
                continue;
            }
            std::vector<llvm::Type *> fields;
            for (const auto &local : function.locals) {
                if (!local.capture) {
                    continue;
                }
                fields.push_back(local.captureMode == FirCaptureMode::View ||
                                         local.captureMode == FirCaptureMode::Edit
                                     ? static_cast<llvm::Type *>(pointerType())
                                     : typeOf(local.type));
            }
            if (fields.empty()) {
                continue;
            }
            closureEnvironmentTypes_[id] =
                llvm::StructType::create(context_, fields, closureEnvironmentName(program_, id));
        }
    }

    void layOutStruct(FirStructId id) {
        std::vector<llvm::Type *> fields;
        if (program_.structs[id].dropFunction.has_value()) {
            fields.push_back(llvm::Type::getInt1Ty(context_));
        }
        for (const auto &field : program_.structs[id].fields) {
            auto *fieldType = typeOf(field.type);
            if (fieldType == nullptr || fieldType->isVoidTy()) {
                fail(program_.structs[id].sourceSpan,
                     "LLVM backend does not support a field of " + program_.structs[id].name);
                fieldType = llvm::Type::getInt8Ty(context_);
            }
            fields.push_back(fieldType);
        }
        if (fields.empty()) {
            fields.push_back(llvm::Type::getInt8Ty(context_));
        }
        structTypes_[id]->setBody(fields);
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
        case TypeKind::Task:
        case TypeKind::Sender:
        case TypeKind::Receiver:
            return pointerType();
        case TypeKind::Channel:
            return channelType_;
        case TypeKind::View:
        case TypeKind::Edit:
            if (type.arguments.size() == 1 && type.arguments.front().kind == TypeKind::Slice) {
                return sliceType(type.arguments.front());
            }
            if (type.arguments.size() == 1 && type.arguments.front().kind == TypeKind::Contract) {
                return typeOf(type.arguments.front());
            }
            return pointerType();
        case TypeKind::Array:
            if (type.arguments.size() == 1) {
                if (auto *element = typeOf(type.arguments.front());
                    element != nullptr && !element->isVoidTy()) {
                    return llvm::ArrayType::get(element, type.declaration);
                }
            }
            return nullptr;
        case TypeKind::Struct:
            return type.declaration < structTypes_.size() ? structTypes_[type.declaration]
                                                          : nullptr;
        case TypeKind::Enum:
            return type.declaration < enumTypes_.size() ? enumTypes_[type.declaration] : nullptr;
        case TypeKind::Invalid:
        case TypeKind::Parameter:
            return nullptr;
        case TypeKind::Slice:
            return sliceType(type);
        case TypeKind::Contract:
            return type.declaration < contractTypes_.size() ? contractTypes_[type.declaration]
                                                            : nullptr;
        case TypeKind::Function:
            return functionValueType_;
        }
        return nullptr;
    }

    llvm::StructType *sliceType(const Type &slice) {
        if (slice.arguments.size() != 1) {
            return nullptr;
        }
        const auto key = llvmTypeKey(slice);
        if (const auto found = sliceTypes_.find(key); found != sliceTypes_.end()) {
            return found->second;
        }
        auto *element = typeOf(slice.arguments.front());
        if (element == nullptr || element->isVoidTy()) {
            return nullptr;
        }
        auto *result = llvm::StructType::create(context_, {pointerType(), sizeType()},
                                                "fdn.slice." + safeName(key));
        sliceTypes_.emplace(key, result);
        return result;
    }

    llvm::FunctionType *callableType(const Type &type) {
        if (type.kind != TypeKind::Function || type.arguments.empty()) {
            return nullptr;
        }
        auto *result = typeOf(type.arguments.front());
        if (result == nullptr) {
            return nullptr;
        }
        std::vector<llvm::Type *> parameters{pointerType()};
        for (std::size_t index = 1; index < type.arguments.size(); ++index) {
            auto *parameter = typeOf(type.arguments[index]);
            if (parameter == nullptr || parameter->isVoidTy()) {
                return nullptr;
            }
            parameters.push_back(parameter);
        }
        return llvm::FunctionType::get(result, parameters, false);
    }

    void declareCallableSupport() {
        functionAdapters_.resize(program_.functions.size());
        closureDrops_.resize(program_.functions.size());
        for (const auto &function : program_.functions) {
            for (const auto &expression : function.expressions) {
                if (const auto *value =
                        std::get_if<FirFunctionValueExpression>(&expression.value)) {
                    if (value->function >= program_.functions.size() ||
                        functionAdapters_[value->function] != nullptr) {
                        continue;
                    }
                    auto *signature = callableType(expression.type);
                    if (signature == nullptr) {
                        fail(expression.span,
                             "LLVM backend cannot lower this function value signature");
                        continue;
                    }
                    functionAdapters_[value->function] = llvm::Function::Create(
                        signature, llvm::GlobalValue::InternalLinkage,
                        functionAdapterName(program_, value->function), module_);
                }
                if (const auto *closure = std::get_if<FirClosureExpression>(&expression.value)) {
                    if (closure->function >= program_.functions.size() ||
                        closureEnvironmentTypes_[closure->function] == nullptr ||
                        closureDrops_[closure->function] != nullptr) {
                        continue;
                    }
                    closureDrops_[closure->function] = llvm::Function::Create(
                        llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {pointerType()},
                                                false),
                        llvm::GlobalValue::InternalLinkage,
                        closureDropName(program_, closure->function), module_);
                }
                if (const auto *contract = std::get_if<FirContractExpression>(&expression.value)) {
                    declareContractSupport(*contract, expression.span);
                }
            }
        }
    }

    void declareContractSupport(const FirContractExpression &expression, SourceSpan span) {
        if (expression.contractType.kind != TypeKind::Contract ||
            expression.contractType.declaration >= program_.contracts.size()) {
            fail(span, "LLVM backend received an invalid contract conversion");
            return;
        }
        const auto key =
            llvmTypeKey(expression.contractType) + ':' + llvmTypeKey(expression.concreteType);
        if (contractSupports_.contains(key)) {
            return;
        }
        ContractSupport support;
        support.contract = expression.contractType;
        support.concrete = expression.concreteType;
        support.targets = expression.methods;
        auto *dropSignature =
            llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {pointerType()}, false);
        support.drop = llvm::Function::Create(
            dropSignature, llvm::GlobalValue::InternalLinkage,
            vtableName(support.contract, support.concrete) + "_drop", module_);
        const auto &contract = program_.contracts[support.contract.declaration];
        if (contract.methods.size() != support.targets.size()) {
            fail(span, "LLVM backend received an incomplete contract vtable");
            return;
        }
        for (std::size_t method = 0; method < contract.methods.size(); ++method) {
            if (support.targets[method].contractDefault ||
                !support.targets[method].delegatePath.empty()) {
                fail(span,
                     "LLVM backend has not lowered delegated or default contract methods yet");
                return;
            }
            std::vector<llvm::Type *> parameters{pointerType()};
            for (const auto &parameter : contract.methods[method].parameters) {
                auto *type = typeOf(parameter);
                if (type == nullptr || type->isVoidTy()) {
                    fail(span, "LLVM backend cannot lower a contract method parameter");
                    return;
                }
                parameters.push_back(type);
            }
            auto *result = typeOf(contract.methods[method].returnType);
            if (result == nullptr) {
                fail(span, "LLVM backend cannot lower a contract method result");
                return;
            }
            support.methods.push_back(llvm::Function::Create(
                llvm::FunctionType::get(result, parameters, false),
                llvm::GlobalValue::InternalLinkage,
                vtableName(support.contract, support.concrete) + "_m" + std::to_string(method),
                module_));
        }
        std::vector<llvm::Constant *> entries;
        entries.push_back(support.drop);
        entries.insert(entries.end(), support.methods.begin(), support.methods.end());
        support.vtable = new llvm::GlobalVariable(
            module_, contractVtableTypes_[support.contract.declaration], true,
            llvm::GlobalValue::InternalLinkage,
            llvm::ConstantStruct::get(contractVtableTypes_[support.contract.declaration], entries),
            vtableName(support.contract, support.concrete));
        contractSupports_.emplace(key, std::move(support));
    }

    void emitCallableSupport() {
        for (FirFunctionId id = 0; id < functionAdapters_.size(); ++id) {
            if (functionAdapters_[id] != nullptr) {
                emitFunctionAdapter(id);
            }
            if (closureDrops_[id] != nullptr) {
                emitClosureDrop(id);
            }
        }
        for (auto &[key, support] : contractSupports_) {
            static_cast<void>(key);
            emitContractSupport(support);
        }
    }

    void emitFunctionAdapter(FirFunctionId id) {
        auto *adapter = functionAdapters_[id];
        auto *block = llvm::BasicBlock::Create(context_, "entry", adapter);
        llvm::IRBuilder<> builder(block);
        std::vector<llvm::Value *> arguments;
        auto argument = adapter->arg_begin();
        ++argument;
        for (; argument != adapter->arg_end(); ++argument) {
            arguments.push_back(&*argument);
        }
        auto *call = builder.CreateCall(functions_[id], arguments);
        if (program_.functions[id].diverges) {
            builder.CreateUnreachable();
        } else if (program_.functions[id].returnType == voidType) {
            builder.CreateRetVoid();
        } else {
            builder.CreateRet(call);
        }
    }

    void emitClosureDrop(FirFunctionId id) {
        auto *function = closureDrops_[id];
        auto *block = llvm::BasicBlock::Create(context_, "entry", function);
        llvm::IRBuilderBase::InsertPointGuard guard(builder_);
        builder_.SetInsertPoint(block);
        auto *environment = function->getArg(0);
        const auto &target = program_.functions[id];
        std::size_t captureIndex{};
        for (FirLocalId local = 0; local < target.locals.size(); ++local) {
            const auto &capture = target.locals[local];
            if (!capture.capture) {
                continue;
            }
            auto *field =
                builder_.CreateStructGEP(closureEnvironmentTypes_[id], environment, captureIndex++);
            if (capture.captureMode != FirCaptureMode::View &&
                capture.captureMode != FirCaptureMode::Edit) {
                dropAddress(field, capture.type);
            }
        }
        builder_.CreateCall(
            runtimeFunction("fdn_dealloc", llvm::Type::getVoidTy(context_), {pointerType()}),
            {environment});
        builder_.CreateRetVoid();
    }

    void emitContractSupport(ContractSupport &support) {
        {
            auto *block = llvm::BasicBlock::Create(context_, "entry", support.drop);
            llvm::IRBuilderBase::InsertPointGuard guard(builder_);
            builder_.SetInsertPoint(block);
            auto *data = support.drop->getArg(0);
            auto *finish = llvm::BasicBlock::Create(context_, "drop", support.drop);
            auto *done = llvm::BasicBlock::Create(context_, "done", support.drop);
            builder_.CreateCondBr(
                builder_.CreateICmpEQ(data, llvm::ConstantPointerNull::get(pointerType())), done,
                finish);
            builder_.SetInsertPoint(finish);
            dropAddress(data, support.concrete);
            builder_.CreateCall(
                runtimeFunction("fdn_dealloc", llvm::Type::getVoidTy(context_), {pointerType()}),
                {data});
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            builder_.CreateRetVoid();
        }
        const auto &contract = program_.contracts[support.contract.declaration];
        for (std::size_t method = 0; method < support.methods.size(); ++method) {
            auto *adapter = support.methods[method];
            auto *block = llvm::BasicBlock::Create(context_, "entry", adapter);
            llvm::IRBuilder<> builder(block);
            const auto target = support.targets[method].function;
            std::vector<llvm::Value *> arguments{adapter->getArg(0)};
            for (std::size_t index = 1; index < adapter->arg_size(); ++index) {
                arguments.push_back(adapter->getArg(index));
            }
            auto *call = builder.CreateCall(functions_[target], arguments);
            if (program_.functions[target].diverges) {
                builder.CreateUnreachable();
            } else if (contract.methods[method].returnType == voidType) {
                builder.CreateRetVoid();
            } else {
                builder.CreateRet(call);
            }
        }
    }

    llvm::FunctionType *functionType(const FirFunction &function) {
        const auto indirectResult = usesExternalResultPointer(function);
        auto *result = function.diverges || indirectResult ? llvm::Type::getVoidTy(context_)
                                                           : typeOf(function.returnType);
        if (result == nullptr) {
            fail(function.sourceSpan,
                 "LLVM backend does not support the return type of " + function.name);
            return nullptr;
        }
        std::vector<llvm::Type *> parameters;
        parameters.reserve(function.parameters.size() + (indirectResult ? 1 : 0) +
                           (function.closure ? 1 : 0));
        if (indirectResult) {
            parameters.push_back(pointerType());
        }
        if (function.closure) {
            parameters.push_back(pointerType());
        }
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
        if (!function.hasBody && function.cSymbol.has_value()) {
            if (containsAggregate(function.returnType) ||
                std::any_of(function.parameters.begin(), function.parameters.end(),
                            [&](const FirLocalId local) {
                                return local < function.locals.size() &&
                                       containsAggregate(function.locals[local].type);
                            })) {
                fail(function.sourceSpan,
                     "LLVM backend does not expose Foundation aggregates through the C ABI: " +
                         function.name);
                return nullptr;
            }
        }
        if (function.task || function.callback || function.stateTransition.has_value() ||
            function.stateTimeout.has_value() || function.workflow.has_value()) {
            fail(function.sourceSpan,
                 "LLVM backend has not lowered the specialized function " + function.name);
            return nullptr;
        }
        return llvm::FunctionType::get(result, parameters, false);
    }

    bool usesExternalResultPointer(const FirFunction &function) const {
        return !function.hasBody && function.cSymbol.has_value() &&
               function.returnType == stringType;
    }

    bool containsAggregate(const Type &type) const {
        if (type.kind == TypeKind::Struct || type.kind == TypeKind::Enum) {
            return true;
        }
        return std::any_of(type.arguments.begin(), type.arguments.end(),
                           [&](const Type &argument) { return containsAggregate(argument); });
    }

    void declareFunctions() {
        functions_.resize(program_.functions.size());
        nativeFunctions_.resize(program_.functions.size());
        for (FirFunctionId id = 0; id < program_.functions.size(); ++id) {
            const auto &function = program_.functions[id];
            auto *signature = functionType(function);
            if (signature == nullptr) {
                continue;
            }
            auto *declaration = llvm::Function::Create(
                signature, llvm::GlobalValue::ExternalLinkage, functionName(program_, id), module_);
            if (function.diverges) {
                declaration->addFnAttr(llvm::Attribute::NoReturn);
            }
            if (usesExternalResultPointer(function)) {
                declaration->addParamAttr(0, llvm::Attribute::getWithStructRetType(
                                                 context_, typeOf(function.returnType)));
            }
            functions_[id] = declaration;
            if (function.cSymbol.has_value()) {
                auto *nativeSignature = nativeFunctionType(function);
                if (nativeSignature != nullptr) {
                    nativeFunctions_[id] =
                        llvm::Function::Create(nativeSignature, llvm::GlobalValue::ExternalLinkage,
                                               *function.cSymbol, module_);
                }
            }
        }
    }

    llvm::Type *abiTypeOf(const Type &type) { return typeOf(type); }

    llvm::FunctionType *nativeFunctionType(const FirFunction &function) {
        auto *result =
            function.diverges ? llvm::Type::getVoidTy(context_) : abiTypeOf(function.returnType);
        if (result == nullptr) {
            fail(function.sourceSpan,
                 "LLVM backend does not support the C ABI return type of " + function.name);
            return nullptr;
        }
        std::vector<llvm::Type *> parameters;
        for (const auto local : function.parameters) {
            auto *parameter = abiTypeOf(function.locals[local].type);
            if (parameter == nullptr || parameter->isVoidTy()) {
                fail(function.sourceSpan,
                     "LLVM backend does not support a C ABI parameter of " + function.name);
                return nullptr;
            }
            parameters.push_back(parameter);
        }
        return llvm::FunctionType::get(result, parameters, false);
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
        captureAddresses_.assign(function_->locals.size(), nullptr);
        for (FirLocalId local = 0; local < function_->locals.size(); ++local) {
            if (function_->locals[local].capture) {
                continue;
            }
            auto *type = typeOf(function_->locals[local].type);
            if (type == nullptr || type->isVoidTy()) {
                fail(function_->sourceSpan,
                     "LLVM backend does not support local " + function_->locals[local].name);
                continue;
            }
            locals_[local] = builder_.CreateAlloca(type, nullptr, "local." + std::to_string(local));
            builder_.CreateStore(llvm::Constant::getNullValue(type), locals_[local]);
        }
        if (diagnostics_.hasErrors()) {
            return;
        }
        auto argument = llvmFunction_->arg_begin();
        if (function_->closure) {
            auto *environment = &*argument++;
            auto *environmentType = closureEnvironmentTypes_[id];
            std::size_t captureIndex{};
            for (FirLocalId local = 0; local < function_->locals.size(); ++local) {
                const auto &capture = function_->locals[local];
                if (!capture.capture) {
                    continue;
                }
                auto *field =
                    builder_.CreateStructGEP(environmentType, environment, captureIndex++);
                if (capture.captureMode == FirCaptureMode::View ||
                    capture.captureMode == FirCaptureMode::Edit) {
                    captureAddresses_[local] =
                        builder_.CreateLoad(pointerType(), field, "capture.address");
                } else {
                    captureAddresses_[local] = field;
                }
            }
        }
        std::size_t parameterIndex{};
        for (; argument != llvmFunction_->arg_end(); ++argument) {
            const auto local = function_->parameters[parameterIndex++];
            builder_.CreateStore(&*argument, locals_[local]);
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
        } else if (const auto *binding = std::get_if<FirLetElseStatement>(&statement.value)) {
            return emitLetElse(*binding, statement.span);
        } else if (const auto *binding = std::get_if<FirResultElseStatement>(&statement.value)) {
            return emitResultElse(*binding, statement.span);
        } else if (const auto *destructure =
                       std::get_if<FirStructDestructureStatement>(&statement.value)) {
            emitStructDestructure(*destructure, statement.span);
        } else if (const auto *assignment = std::get_if<FirAssignmentStatement>(&statement.value)) {
            const auto value = emitExpression(assignment->value);
            if (!value.diverges) {
                if (auto *address = emitAddress(assignment->target); address != nullptr) {
                    dropAddress(address, function_->expressions[assignment->target].type);
                    builder_.CreateStore(value.value, address);
                }
            }
        } else if (const auto *expression = std::get_if<FirExpressionStatement>(&statement.value)) {
            emitExpression(expression->expression);
        } else if (const auto *discard = std::get_if<FirDiscardStatement>(&statement.value)) {
            const auto value = emitExpression(discard->expression);
            if (!value.diverges) {
                dropValue(value.value, function_->expressions[discard->expression].type);
            }
        } else if (const auto *returned = std::get_if<FirReturnStatement>(&statement.value)) {
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
        } else if (const auto *loop = std::get_if<FirForStatement>(&statement.value)) {
            emitFor(*loop, statement.span);
        } else if (const auto *broken = std::get_if<FirBreakStatement>(&statement.value)) {
            dropLocals(broken->drops);
            if (loops_.empty()) {
                fail(statement.span, "LLVM backend received break outside a loop");
            } else {
                builder_.CreateBr(loops_.back().breakBlock);
            }
        } else if (const auto *continued = std::get_if<FirContinueStatement>(&statement.value)) {
            dropLocals(continued->drops);
            if (loops_.empty()) {
                fail(statement.span, "LLVM backend received continue outside a loop");
            } else {
                builder_.CreateBr(loops_.back().continueBlock);
            }
        } else if (const auto *unsafe = std::get_if<FirUnsafeStatement>(&statement.value)) {
            return emitBlock(unsafe->body);
        } else {
            fail(statement.span, "LLVM backend has not lowered this statement yet");
        }
        return builder_.GetInsertBlock()->getTerminator() != nullptr;
    }

    bool emitLetElse(const FirLetElseStatement &binding, SourceSpan span) {
        const auto initializer = emitExpression(binding.initializer);
        if (initializer.diverges) {
            return true;
        }
        const auto resultType = function_->expressions[binding.initializer].type;
        auto *result = valueAddress(initializer.value, resultType, "let.result");
        auto *tag = enumTag(result, resultType, span);
        if (tag == nullptr) {
            return true;
        }
        auto *failed = llvm::BasicBlock::Create(context_, "let.failed", llvmFunction_);
        auto *success = llvm::BasicBlock::Create(context_, "let.success", llvmFunction_);
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
            failed, success);

        builder_.SetInsertPoint(failed);
        if (!bindEnumPayload(result, resultType, 1, binding.errorLocal, true, span)) {
            return true;
        }
        if (!emitBlock(binding.elseBlock)) {
            fail(span, "LLVM backend requires the else branch of a Result binding to exit");
            if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                builder_.CreateUnreachable();
            }
        }

        builder_.SetInsertPoint(success);
        if (!bindEnumPayload(result, resultType, 0, binding.local, true, span)) {
            return true;
        }
        return false;
    }

    bool emitResultElse(const FirResultElseStatement &binding, SourceSpan span) {
        const auto initializer = emitExpression(binding.expression);
        if (initializer.diverges) {
            return true;
        }
        const auto resultType = function_->expressions[binding.expression].type;
        auto *result = valueAddress(initializer.value, resultType, "result.else");
        auto *tag = enumTag(result, resultType, span);
        if (tag == nullptr) {
            return true;
        }
        auto *failed = llvm::BasicBlock::Create(context_, "result.failed", llvmFunction_);
        auto *success = llvm::BasicBlock::Create(context_, "result.success", llvmFunction_);
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
            failed, success);

        builder_.SetInsertPoint(failed);
        if (!bindEnumPayload(result, resultType, 1, binding.errorLocal, true, span)) {
            return true;
        }
        if (!emitBlock(binding.elseBlock)) {
            fail(span, "LLVM backend requires the else branch of Result handling to exit");
            if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                builder_.CreateUnreachable();
            }
        }
        builder_.SetInsertPoint(success);
        return false;
    }

    void emitStructDestructure(const FirStructDestructureStatement &destructure, SourceSpan span) {
        const auto initializer = emitExpression(destructure.initializer);
        if (initializer.diverges) {
            return;
        }
        auto sourceType = function_->expressions[destructure.initializer].type;
        llvm::Value *source{};
        if (destructure.owned) {
            source = initializer.value;
            if (sourceType.kind == TypeKind::Own && sourceType.arguments.size() == 1) {
                sourceType = sourceType.arguments.front();
            }
        } else {
            source = valueAddress(initializer.value, sourceType, "destructure.value");
        }
        if (sourceType.kind == TypeKind::Channel) {
            for (const auto &binding : destructure.bindings) {
                if (binding.field >= 2 || binding.local >= locals_.size()) {
                    fail(span, "LLVM backend received an invalid channel destructure binding");
                    return;
                }
                auto *field = builder_.CreateStructGEP(channelType_, source, binding.field,
                                                       "channel.field.address");
                builder_.CreateStore(moveFromAddress(field, function_->locals[binding.local].type),
                                     locals_[binding.local]);
            }
            dropAddress(source, sourceType);
            return;
        }
        if (sourceType.kind != TypeKind::Struct ||
            sourceType.declaration >= program_.structs.size()) {
            fail(span, "LLVM backend received a non-struct destructure");
            return;
        }
        for (const auto &binding : destructure.bindings) {
            if (binding.local >= locals_.size()) {
                fail(span, "LLVM backend received an invalid destructure binding");
                return;
            }
            auto *field = structFieldAddress(source, sourceType, binding.field, span);
            if (field == nullptr) {
                return;
            }
            const auto &type = function_->locals[binding.local].type;
            builder_.CreateStore(moveFromAddress(field, type), locals_[binding.local]);
        }
        if (destructure.owned) {
            dropValue(initializer.value, function_->expressions[destructure.initializer].type);
        } else {
            dropAddress(source, sourceType);
        }
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

    void emitFor(const FirForStatement &loop, SourceSpan span) {
        if (loop.next.has_value()) {
            fail(span, "LLVM backend has not lowered iterator for loops yet");
            return;
        }
        const auto sequence = emitExpression(loop.sequence);
        if (sequence.diverges || loop.sequenceStorage >= locals_.size() ||
            loop.index >= locals_.size() || loop.value >= locals_.size()) {
            return;
        }
        builder_.CreateStore(sequence.value, locals_[loop.sequenceStorage]);
        builder_.CreateStore(llvm::ConstantInt::get(sizeType(), 0), locals_[loop.index]);
        auto *conditionBlock = llvm::BasicBlock::Create(context_, "for.condition", llvmFunction_);
        auto *bodyBlock = llvm::BasicBlock::Create(context_, "for.body", llvmFunction_);
        auto *nextBlock = llvm::BasicBlock::Create(context_, "for.next", llvmFunction_);
        auto *exitBlock = llvm::BasicBlock::Create(context_, "for.end", llvmFunction_);
        builder_.CreateBr(conditionBlock);
        builder_.SetInsertPoint(conditionBlock);
        auto *index = loadLocal(loop.index);
        auto *length = builder_.CreateExtractValue(loadLocal(loop.sequenceStorage), 1);
        builder_.CreateCondBr(builder_.CreateICmpULT(index, length), bodyBlock, exitBlock);
        builder_.SetInsertPoint(bodyBlock);
        auto *data = builder_.CreateExtractValue(loadLocal(loop.sequenceStorage), 0);
        const auto &valueType = function_->locals[loop.value].type;
        auto *elementType = valueType.kind == TypeKind::View || valueType.kind == TypeKind::Edit
                                ? typeOf(valueType.arguments.front())
                                : typeOf(valueType);
        auto *element = builder_.CreateInBoundsGEP(elementType, data, index);
        if (valueType.kind == TypeKind::View || valueType.kind == TypeKind::Edit) {
            builder_.CreateStore(element, locals_[loop.value]);
        } else {
            builder_.CreateStore(builder_.CreateLoad(elementType, element), locals_[loop.value]);
        }
        loops_.push_back({exitBlock, nextBlock});
        const auto exits = emitBlock(loop.body);
        loops_.pop_back();
        if (!exits) {
            builder_.CreateBr(nextBlock);
        }
        builder_.SetInsertPoint(nextBlock);
        auto *next = builder_.CreateCall(
            runtimeFunction("fdn_usize_add", sizeType(), {sizeType(), sizeType()}),
            {loadLocal(loop.index), llvm::ConstantInt::get(sizeType(), 1)});
        builder_.CreateStore(next, locals_[loop.index]);
        builder_.CreateBr(conditionBlock);
        builder_.SetInsertPoint(exitBlock);
        if (loop.ownsSequence) {
            dropValue(loadLocal(loop.sequenceStorage),
                      function_->locals[loop.sequenceStorage].type);
        }
    }

    EmittedValue emitExpression(FirExpressionId id) {
        if (id >= function_->expressions.size()) {
            fail(function_->sourceSpan, "LLVM backend received an invalid expression");
            return {};
        }
        const auto &expression = function_->expressions[id];
        setLocation(expression.span);
        if (const auto *integer = std::get_if<FirIntegerExpression>(&expression.value)) {
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
        if (const auto *floating = std::get_if<FirFloatingExpression>(&expression.value)) {
            auto *type = typeOf(expression.type);
            return {llvm::ConstantFP::get(type, floating->text), false};
        }
        if (const auto *boolean = std::get_if<FirBooleanExpression>(&expression.value)) {
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
        if (const auto *array = std::get_if<FirArrayExpression>(&expression.value)) {
            return emitArray(*array, expression.type);
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
            const auto &localType = function_->locals[read->local].type;
            if (localType.kind == TypeKind::View && localType.arguments.size() == 1 &&
                (localType.arguments.front().kind == TypeKind::Slice ||
                 localType.arguments.front().kind == TypeKind::Contract)) {
                return {address, false};
            }
            return {builder_.CreateLoad(target, address, "read"), false};
        }
        if (const auto *moved = std::get_if<FirMoveExpression>(&expression.value)) {
            auto *address = localAddress(moved->local);
            if (address == nullptr) {
                fail(expression.span, "LLVM backend received an invalid move source");
                return {};
            }
            return {moveFromAddress(address, expression.type), false};
        }
        if (const auto *ownership = std::get_if<FirOwnershipExpression>(&expression.value)) {
            return emitOwnership(*ownership, expression.type, expression.span);
        }
        if (const auto *functionValue =
                std::get_if<FirFunctionValueExpression>(&expression.value)) {
            return emitFunctionValue(*functionValue, expression.type, expression.span);
        }
        if (const auto *closure = std::get_if<FirClosureExpression>(&expression.value)) {
            return emitClosure(*closure, expression.type, expression.span);
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
        if (const auto *contract = std::get_if<FirContractExpression>(&expression.value)) {
            return emitContract(*contract, expression.type, expression.span);
        }
        if (const auto *literal = std::get_if<FirStructExpression>(&expression.value)) {
            return emitStruct(*literal, expression.span);
        }
        if (const auto *field = std::get_if<FirFieldExpression>(&expression.value)) {
            return emitField(*field, expression.type, expression.span);
        }
        if (const auto *replace = std::get_if<FirReplaceExpression>(&expression.value)) {
            return emitReplace(*replace, expression.type, expression.span);
        }
        if (const auto *constructor = std::get_if<FirEnumExpression>(&expression.value)) {
            return emitEnum(*constructor, expression.span);
        }
        if (const auto *index = std::get_if<FirIndexExpression>(&expression.value)) {
            auto *address = emitIndexAddress(*index, expression.span);
            auto *type = typeOf(expression.type);
            return address == nullptr || type == nullptr
                       ? EmittedValue{}
                       : EmittedValue{builder_.CreateLoad(type, address, "element.value"), false};
        }
        if (const auto *pointer = std::get_if<FirRawPointerExpression>(&expression.value)) {
            return emitRawPointer(*pointer, expression.span);
        }
        if (const auto *conditional = std::get_if<FirConditionalExpression>(&expression.value)) {
            return emitConditional(*conditional, expression.type);
        }
        if (const auto *match = std::get_if<FirMatchExpression>(&expression.value)) {
            return emitMatch(*match, expression.type, expression.span);
        }
        fail(expression.span, "LLVM backend has not lowered this expression yet");
        return {};
    }

    EmittedValue emitArray(const FirArrayExpression &array, const Type &type) {
        auto *arrayType = llvm::dyn_cast_or_null<llvm::ArrayType>(typeOf(type));
        if (arrayType == nullptr || array.elements.size() > arrayType->getNumElements()) {
            fail({}, "LLVM backend received an invalid array literal");
            return {};
        }
        auto *storage = builder_.CreateAlloca(arrayType, nullptr, "array.literal");
        builder_.CreateStore(llvm::Constant::getNullValue(arrayType), storage);
        for (std::size_t index = 0; index < array.elements.size(); ++index) {
            const auto value = emitExpression(array.elements[index]);
            if (value.diverges) {
                return value;
            }
            auto *address = builder_.CreateInBoundsGEP(
                arrayType, storage,
                {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0),
                 llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), index)});
            builder_.CreateStore(value.value, address);
        }
        return {builder_.CreateLoad(arrayType, storage), false};
    }

    EmittedValue emitFunctionValue(const FirFunctionValueExpression &function, const Type &,
                                   SourceSpan span) {
        if (function.function >= functionAdapters_.size() ||
            functionAdapters_[function.function] == nullptr) {
            fail(span, "LLVM backend received an invalid function value");
            return {};
        }
        llvm::Value *value = llvm::PoisonValue::get(functionValueType_);
        value = builder_.CreateInsertValue(value, llvm::ConstantPointerNull::get(pointerType()), 0);
        value = builder_.CreateInsertValue(value, functionAdapters_[function.function], 1);
        value = builder_.CreateInsertValue(value, llvm::ConstantPointerNull::get(pointerType()), 2);
        return {value, false};
    }

    EmittedValue emitClosure(const FirClosureExpression &closure, const Type &, SourceSpan span) {
        if (closure.function >= functions_.size() || functions_[closure.function] == nullptr) {
            fail(span, "LLVM backend received an invalid closure");
            return {};
        }
        llvm::Value *environment = llvm::ConstantPointerNull::get(pointerType());
        llvm::Value *drop = llvm::ConstantPointerNull::get(pointerType());
        if (!closure.captures.empty()) {
            auto *environmentType = closureEnvironmentTypes_[closure.function];
            if (environmentType == nullptr) {
                fail(span, "LLVM backend received an invalid closure environment");
                return {};
            }
            environment = allocate(environmentType, "closure.environment");
            const auto &target = program_.functions[closure.function];
            std::size_t captureIndex{};
            FirLocalId targetLocal{};
            for (const auto &capture : closure.captures) {
                while (targetLocal < target.locals.size() && !target.locals[targetLocal].capture) {
                    ++targetLocal;
                }
                if (targetLocal >= target.locals.size()) {
                    fail(span, "LLVM backend received an invalid closure capture");
                    return {};
                }
                auto *field =
                    builder_.CreateStructGEP(environmentType, environment, captureIndex++);
                if (capture.mode == FirCaptureMode::View || capture.mode == FirCaptureMode::Edit) {
                    auto *address = localAddress(capture.local);
                    if (address == nullptr) {
                        fail(span, "LLVM backend cannot borrow this closure capture");
                        return {};
                    }
                    builder_.CreateStore(address, field);
                } else {
                    auto *value = loadLocal(capture.local);
                    builder_.CreateStore(value, field);
                    if (capture.mode == FirCaptureMode::Own) {
                        builder_.CreateStore(llvm::Constant::getNullValue(value->getType()),
                                             localAddress(capture.local));
                    }
                }
                ++targetLocal;
            }
            drop = closureDrops_[closure.function];
        }
        llvm::Value *value = llvm::PoisonValue::get(functionValueType_);
        value = builder_.CreateInsertValue(value, environment, 0);
        value = builder_.CreateInsertValue(value, functions_[closure.function], 1);
        value = builder_.CreateInsertValue(value, drop, 2);
        return {value, false};
    }

    EmittedValue emitOwnership(const FirOwnershipExpression &ownership, const Type &type,
                               SourceSpan span) {
        if (ownership.operand >= function_->expressions.size()) {
            fail(span, "LLVM backend received an invalid ownership operand");
            return {};
        }
        const auto &operandType = function_->expressions[ownership.operand].type;
        if (ownership.operation == FirOwnershipOperator::View && type.kind != TypeKind::View) {
            return emitExpression(ownership.operand);
        }
        if (ownership.operation == FirOwnershipOperator::Own) {
            if (type.kind != TypeKind::Own || type.arguments.size() != 1) {
                fail(span, "LLVM backend received an invalid own expression");
                return {};
            }
            const auto operand = emitExpression(ownership.operand);
            if (operand.diverges) {
                return operand;
            }
            auto *target = typeOf(type.arguments.front());
            if (target == nullptr || !target->isSized()) {
                fail(span, "LLVM backend cannot allocate this owned value");
                return {};
            }
            auto *storage = allocate(target, "owned.value");
            builder_.CreateStore(operand.value, storage);
            return {storage, false};
        }
        if (type.arguments.size() == 1 && type.arguments.front().kind == TypeKind::Slice) {
            if (operandType.kind == TypeKind::View || operandType.kind == TypeKind::Edit ||
                operandType.kind == TypeKind::Slice) {
                return emitExpression(ownership.operand);
            }
            auto array = operandType;
            llvm::Value *address{};
            if (array.kind == TypeKind::Own && array.arguments.size() == 1) {
                const auto operand = emitExpression(ownership.operand);
                if (operand.diverges) {
                    return operand;
                }
                address = operand.value;
                array = array.arguments.front();
            } else {
                address = emitAddress(ownership.operand);
            }
            if (array.kind != TypeKind::Array || array.arguments.size() != 1 ||
                address == nullptr) {
                fail(span, "LLVM backend cannot form this slice borrow");
                return {};
            }
            auto *arrayType = llvm::dyn_cast_or_null<llvm::ArrayType>(typeOf(array));
            auto *slice = typeOf(type);
            if (arrayType == nullptr || slice == nullptr) {
                fail(span, "LLVM backend cannot lay out this slice borrow");
                return {};
            }
            auto *data = builder_.CreateInBoundsGEP(
                arrayType, address,
                {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0),
                 llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0)});
            llvm::Value *value = llvm::PoisonValue::get(slice);
            value = builder_.CreateInsertValue(value, data, 0);
            value = builder_.CreateInsertValue(
                value, llvm::ConstantInt::get(sizeType(), array.declaration), 1);
            return {value, false};
        }
        if (operandType.kind == TypeKind::Own || operandType.kind == TypeKind::View ||
            operandType.kind == TypeKind::Edit) {
            return emitExpression(ownership.operand);
        }
        if (auto *address = emitAddress(ownership.operand); address != nullptr) {
            return {address, false};
        }
        const auto operand = emitExpression(ownership.operand);
        if (operand.diverges) {
            return operand;
        }
        if (typeRequiresDrop(operandType)) {
            fail(span, "LLVM backend cannot borrow a temporary value that requires drop yet");
            return {};
        }
        return {valueAddress(operand.value, operandType, "borrow.temporary"), false};
    }

    EmittedValue emitContract(const FirContractExpression &contract, const Type &type,
                              SourceSpan span) {
        const auto value = emitExpression(contract.value);
        if (value.diverges) {
            return value;
        }
        const auto key =
            llvmTypeKey(contract.contractType) + ':' + llvmTypeKey(contract.concreteType);
        const auto found = contractSupports_.find(key);
        if (found == contractSupports_.end()) {
            fail(span, "LLVM backend received an unregistered contract conversion");
            return {};
        }
        llvm::Value *contractValue = llvm::PoisonValue::get(typeOf(contract.contractType));
        contractValue = builder_.CreateInsertValue(contractValue, value.value, 0);
        contractValue = builder_.CreateInsertValue(contractValue, found->second.vtable, 1);
        if (type.kind != TypeKind::Own) {
            return {contractValue, false};
        }
        auto *storage = allocate(typeOf(contract.contractType), "owned.contract");
        builder_.CreateStore(contractValue, storage);
        return {storage, false};
    }

    EmittedValue emitStruct(const FirStructExpression &literal, SourceSpan span) {
        if (literal.type.kind != TypeKind::Struct ||
            literal.type.declaration >= program_.structs.size()) {
            fail(span, "LLVM backend received an invalid struct literal");
            return {};
        }
        auto *layout = llvm::dyn_cast_or_null<llvm::StructType>(typeOf(literal.type));
        if (layout == nullptr) {
            fail(span, "LLVM backend cannot lay out this struct literal");
            return {};
        }
        llvm::Value *value = llvm::Constant::getNullValue(layout);
        const auto &declaration = program_.structs[literal.type.declaration];
        if (declaration.dropFunction.has_value()) {
            value = builder_.CreateInsertValue(value, llvm::ConstantInt::getTrue(context_), 0);
        }
        for (const auto &field : literal.fields) {
            if (field.field >= declaration.fields.size()) {
                fail(span, "LLVM backend received an invalid struct field");
                return {};
            }
            const auto emitted = emitExpression(field.value);
            if (emitted.diverges) {
                return emitted;
            }
            value = builder_.CreateInsertValue(
                value, emitted.value, structFieldIndex(literal.type.declaration, field.field));
        }
        return {value, false};
    }

    EmittedValue emitField(const FirFieldExpression &field, const Type &type, SourceSpan span) {
        const auto baseType = function_->expressions[field.base].type;
        if (baseType.kind == TypeKind::Own || baseType.kind == TypeKind::View ||
            baseType.kind == TypeKind::Edit) {
            if (baseType.arguments.size() != 1) {
                fail(span, "LLVM backend received an invalid field receiver");
                return {};
            }
            const auto base = emitExpression(field.base);
            if (base.diverges) {
                return base;
            }
            auto *address =
                structFieldAddress(base.value, baseType.arguments.front(), field.field, span);
            return address == nullptr
                       ? EmittedValue{}
                       : EmittedValue{builder_.CreateLoad(typeOf(type), address, "field.value"),
                                      false};
        }
        if (isPlaceExpression(field.base)) {
            auto *address = emitAddress(field.base);
            if (address == nullptr) {
                return {};
            }
            address = structFieldAddress(address, baseType, field.field, span);
            return address == nullptr
                       ? EmittedValue{}
                       : EmittedValue{builder_.CreateLoad(typeOf(type), address, "field.value"),
                                      false};
        }
        const auto base = emitExpression(field.base);
        if (base.diverges) {
            return base;
        }
        if (baseType.kind != TypeKind::Struct || baseType.declaration >= program_.structs.size() ||
            field.field >= program_.structs[baseType.declaration].fields.size()) {
            fail(span, "LLVM backend received an invalid field access");
            return {};
        }
        return {builder_.CreateExtractValue(base.value,
                                            structFieldIndex(baseType.declaration, field.field)),
                false};
    }

    EmittedValue emitReplace(const FirReplaceExpression &replace, const Type &type,
                             SourceSpan span) {
        const auto replacement = emitExpression(replace.value);
        if (replacement.diverges) {
            return replacement;
        }
        auto *address = emitAddress(replace.target);
        if (address == nullptr) {
            fail(span, "LLVM backend received an invalid replace target");
            return {};
        }
        auto *previous = moveFromAddress(address, type);
        builder_.CreateStore(replacement.value, address);
        return {previous, false};
    }

    EmittedValue emitEnum(const FirEnumExpression &constructor, SourceSpan span) {
        if (constructor.type.kind != TypeKind::Enum ||
            constructor.type.declaration >= program_.enums.size() ||
            constructor.variant >= program_.enums[constructor.type.declaration].variants.size()) {
            fail(span, "LLVM backend received an invalid enum constructor");
            return {};
        }
        auto *layout = llvm::dyn_cast_or_null<llvm::StructType>(typeOf(constructor.type));
        llvm::Value *value = llvm::Constant::getNullValue(layout);
        value = builder_.CreateInsertValue(
            value, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), constructor.variant),
            0);
        if (constructor.payload.has_value()) {
            const auto payload = emitExpression(*constructor.payload);
            if (payload.diverges) {
                return payload;
            }
            value = builder_.CreateInsertValue(value, payload.value, constructor.variant + 1);
        }
        return {value, false};
    }

    EmittedValue emitMatch(const FirMatchExpression &match, const Type &type, SourceSpan span) {
        const auto inspected = emitExpression(match.value);
        if (inspected.diverges) {
            return inspected;
        }
        if (match.type.kind != TypeKind::Enum || match.type.declaration >= program_.enums.size()) {
            fail(span, "LLVM backend received an invalid match type");
            return {};
        }
        const auto inspectedType = function_->expressions[match.value].type;
        llvm::Value *storage{};
        if (inspectedType.kind == TypeKind::Own || inspectedType.kind == TypeKind::View ||
            inspectedType.kind == TypeKind::Edit) {
            storage = inspected.value;
        } else {
            storage = valueAddress(inspected.value, inspectedType, "match.value");
        }

        std::vector<EmittedValue> patterns;
        patterns.reserve(match.arms.size());
        for (const auto &arm : match.arms) {
            if (!arm.pattern.has_value()) {
                patterns.push_back({});
                continue;
            }
            patterns.push_back(emitExpression(*arm.pattern));
            if (patterns.back().diverges) {
                return patterns.back();
            }
        }

        llvm::AllocaInst *result{};
        if (type != voidType && type != neverType) {
            auto *resultType = typeOf(type);
            if (resultType == nullptr || resultType->isVoidTy()) {
                fail(span, "LLVM backend cannot lay out the result of this match");
                return {};
            }
            result = builder_.CreateAlloca(resultType, nullptr, "match.result");
            builder_.CreateStore(llvm::Constant::getNullValue(resultType), result);
        }

        auto *merge = llvm::BasicBlock::Create(context_, "match.end", llvmFunction_);
        auto hasMergeIncoming = false;
        for (std::size_t index = 0; index < match.arms.size(); ++index) {
            const auto &arm = match.arms[index];
            auto *body = llvm::BasicBlock::Create(context_, "match.arm", llvmFunction_);
            auto *next = llvm::BasicBlock::Create(context_, "match.next", llvmFunction_);

            if (arm.wildcard) {
                builder_.CreateBr(body);
            } else {
                if (arm.variant >= program_.enums[match.type.declaration].variants.size()) {
                    fail(span, "LLVM backend received an invalid match variant");
                    return {};
                }
                auto *tag = enumTag(storage, match.type, span);
                llvm::Value *condition = builder_.CreateICmpEQ(
                    tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), arm.variant));
                if (arm.pattern.has_value()) {
                    const auto &payloadType =
                        program_.enums[match.type.declaration].variants[arm.variant].payload;
                    if (!payloadType.has_value()) {
                        fail(span, "LLVM backend received a payload pattern for an empty variant");
                        return {};
                    }
                    auto *payload = enumPayloadAddress(storage, match.type, arm.variant, span);
                    auto *value =
                        builder_.CreateLoad(typeOf(*payloadType), payload, "match.pattern.value");
                    auto *equal = emitEqual(value, patterns[index].value, *payloadType, span);
                    if (equal == nullptr) {
                        return {};
                    }
                    condition = builder_.CreateAnd(condition, equal);
                }
                builder_.CreateCondBr(condition, body, next);
            }

            builder_.SetInsertPoint(body);
            if (arm.guardBinding.has_value()) {
                if (!bindEnumPayload(storage, match.type, arm.variant, *arm.guardBinding, false,
                                     span)) {
                    return {};
                }
            }
            if (arm.guard.has_value()) {
                const auto guard = emitExpression(*arm.guard);
                if (!guard.diverges) {
                    auto *accepted =
                        llvm::BasicBlock::Create(context_, "match.guard", llvmFunction_);
                    builder_.CreateCondBr(guard.value, accepted, next);
                    builder_.SetInsertPoint(accepted);
                }
            }

            if (builder_.GetInsertBlock()->getTerminator() == nullptr && arm.binding.has_value()) {
                if (!bindEnumPayload(storage, match.type, arm.variant, *arm.binding, true, span)) {
                    return {};
                }
            }

            auto exits = builder_.GetInsertBlock()->getTerminator() != nullptr;
            if (!exits) {
                for (const auto statement : function_->blocks[arm.block].statements) {
                    if (emitStatement(statement)) {
                        exits = true;
                        break;
                    }
                }
            }
            if (!exits) {
                EmittedValue armValue;
                if (arm.expression.has_value()) {
                    armValue = emitExpression(*arm.expression);
                    exits = armValue.diverges;
                } else if (result != nullptr) {
                    fail(span, "LLVM backend received a value match arm without a value");
                    return {};
                }
                if (!exits) {
                    if (result != nullptr) {
                        builder_.CreateStore(armValue.value, result);
                    }
                    dropLocals(arm.drops);
                    dropMatchValue(storage, inspected.value, inspectedType);
                    builder_.CreateBr(merge);
                    hasMergeIncoming = true;
                }
            }
            builder_.SetInsertPoint(next);
        }

        setLocation(span);
        builder_.CreateCall(
            runtimeFunction("fdn_invalid_enum_tag", llvm::Type::getVoidTy(context_), {}));
        builder_.CreateUnreachable();
        if (!hasMergeIncoming) {
            merge->eraseFromParent();
            return {llvm::PoisonValue::get(llvm::Type::getInt8Ty(context_)), true};
        }
        builder_.SetInsertPoint(merge);
        if (result == nullptr) {
            return {nullptr, false};
        }
        return {builder_.CreateLoad(result->getAllocatedType(), result, "match.result.value"),
                false};
    }

    llvm::Value *allocate(llvm::Type *type, std::string_view name) {
        if (type == nullptr || !type->isSized()) {
            fail(function_->sourceSpan, "LLVM backend cannot allocate this value");
            return nullptr;
        }
        llvm::Value *size = llvm::ConstantExpr::getSizeOf(type);
        if (size->getType() != sizeType()) {
            size = builder_.CreateIntCast(size, sizeType(), false);
        }
        return builder_.CreateCall(runtimeFunction("fdn_alloc", pointerType(), {sizeType()}),
                                   {size}, name);
    }

    EmittedValue emitUnary(const FirUnaryExpression &unary, const Type &type, SourceSpan span) {
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
                auto callee =
                    runtimeFunction("fdn_" + tag + "_negate", typeOf(type), {typeOf(type)});
                setLocation(span);
                return {builder_.CreateCall(callee, {operand.value}), false};
            }
            break;
        case FirUnaryOperator::Not:
            return {builder_.CreateNot(operand.value), false};
        case FirUnaryOperator::Empty:
            if (type == boolType && function_->expressions[unary.operand].type == stringType) {
                auto *length = builder_.CreateExtractValue(operand.value, 1);
                return {builder_.CreateICmpEQ(length, llvm::ConstantInt::get(sizeType(), 0)),
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

    EmittedValue emitBinary(const FirBinaryExpression &binary, const Type &type, SourceSpan span) {
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
            if ((type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) &&
                binary.operation == FirBinaryOperator::Add && type.arguments.size() == 1) {
                return {builder_.CreateInBoundsGEP(typeOf(type.arguments.front()), left.value,
                                                   integerToSize(right.value)),
                        false};
            }
            if (type == stringType && binary.operation == FirBinaryOperator::Add) {
                auto *result = builder_.CreateAlloca(stringType_, nullptr, "string.concat.result");
                builder_.CreateCall(
                    runtimeFunction("fdn_abi_string_concat", llvm::Type::getVoidTy(context_),
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
                auto callee = runtimeFunction("fdn_" + integerTypeTag(type) + "_" + operation,
                                              typeOf(type), {typeOf(type), typeOf(type)});
                return {builder_.CreateCall(callee, {left.value, right.value}), false};
            }
            break;
        case FirBinaryOperator::Equal:
        case FirBinaryOperator::NotEqual:
            if (function_->expressions[binary.left].type == stringType) {
                auto callee =
                    runtimeFunction("fdn_abi_string_equal", llvm::Type::getInt32Ty(context_),
                                    {pointerType(), pointerType()});
                auto *equal = builder_.CreateICmpNE(
                    builder_.CreateCall(callee,
                                        {stringAddress(left.value), stringAddress(right.value)}),
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0));
                return {binary.operation == FirBinaryOperator::Equal ? equal
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

    EmittedValue emitComparison(FirBinaryOperator operation, llvm::Value *left, llvm::Value *right,
                                Type type) {
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

    EmittedValue emitCall(const FirCallExpression &call, const Type &type, SourceSpan span) {
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
        if (!call.argumentParameters.empty()) {
            if (call.argumentParameters.size() != values.size()) {
                fail(span, "LLVM call argument mapping has the wrong size");
                return {};
            }
            std::vector<llvm::Value *> ordered(values.size());
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (call.argumentParameters[index] >= ordered.size()) {
                    fail(span, "LLVM call argument mapping is invalid");
                    return {};
                }
                ordered[call.argumentParameters[index]] = values[index];
            }
            values = std::move(ordered);
        }
        setLocation(span);
        llvm::Value *result{};
        switch (call.kind) {
        case FirCallKind::Function:
            if (call.function >= functions_.size() || functions_[call.function] == nullptr) {
                fail(span, "LLVM backend received an invalid function call");
                return {};
            }
            if (usesExternalResultPointer(program_.functions[call.function])) {
                auto *storage = builder_.CreateAlloca(typeOf(type), nullptr, "external.result");
                auto invocationValues = values;
                invocationValues.insert(invocationValues.begin(), storage);
                auto *invocation = builder_.CreateCall(functions_[call.function], invocationValues);
                invocation->addParamAttr(
                    0, llvm::Attribute::getWithStructRetType(context_, typeOf(type)));
                result = builder_.CreateLoad(typeOf(type), storage, "external.result.value");
            } else {
                result = builder_.CreateCall(functions_[call.function], values);
            }
            break;
        case FirCallKind::Print:
            if (values.size() != 1) {
                fail(span, "LLVM print call has invalid arguments");
                return {};
            }
            result = builder_.CreateCall(runtimeFunction("fdn_abi_println",
                                                         llvm::Type::getVoidTy(context_),
                                                         {pointerType()}),
                                         {stringAddress(values.front())});
            break;
        case FirCallKind::Panic:
            if (values.size() != 1) {
                fail(span, "LLVM panic call has invalid arguments");
                return {};
            }
            builder_.CreateCall(
                runtimeFunction("fdn_abi_panic", llvm::Type::getVoidTy(context_), {pointerType()}),
                {stringAddress(values.front())});
            builder_.CreateUnreachable();
            return {llvm::PoisonValue::get(llvm::Type::getInt8Ty(context_)), true};
        case FirCallKind::Len:
            if (values.size() == 1) {
                auto sequence = function_->expressions[call.arguments.front()].type;
                if (sequence == stringType) {
                    result = builder_.CreateExtractValue(values.front(), 1);
                    break;
                }
                if (sequence.kind == TypeKind::Array) {
                    result = llvm::ConstantInt::get(sizeType(), sequence.declaration);
                    break;
                }
                if (sequence.kind == TypeKind::Slice) {
                    result = builder_.CreateExtractValue(values.front(), 1);
                    break;
                }
                if ((sequence.kind == TypeKind::View || sequence.kind == TypeKind::Edit) &&
                    sequence.arguments.size() == 1 &&
                    sequence.arguments.front().kind == TypeKind::Slice) {
                    result = builder_.CreateExtractValue(values.front(), 1);
                    break;
                }
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
            result = builder_.CreateICmpEQ(values.front(),
                                           llvm::ConstantPointerNull::get(pointerType()));
            break;
        case FirCallKind::NumericConversion:
            if (values.size() != 1) {
                fail(span, "LLVM numeric conversion has invalid arguments");
                return {};
            }
            result = emitNumericConversion(
                values.front(), function_->expressions[call.arguments.front()].type, type, span);
            if (result == nullptr) {
                return {};
            }
            break;
        case FirCallKind::FunctionValue: {
            if (call.local >= function_->locals.size()) {
                fail(span, "LLVM backend received an invalid callable local");
                return {};
            }
            auto callableTypeValue = function_->locals[call.local].type;
            llvm::Value *callable = loadLocal(call.local);
            if ((callableTypeValue.kind == TypeKind::View ||
                 callableTypeValue.kind == TypeKind::Edit) &&
                callableTypeValue.arguments.size() == 1) {
                callableTypeValue = callableTypeValue.arguments.front();
                callable = builder_.CreateLoad(functionValueType_, callable);
            }
            auto *signature = callableType(callableTypeValue);
            if (signature == nullptr) {
                fail(span, "LLVM backend cannot lower this callable signature");
                return {};
            }
            auto *environment = builder_.CreateExtractValue(callable, 0);
            auto *target = builder_.CreateExtractValue(callable, 1);
            values.insert(values.begin(), environment);
            result = builder_.CreateCall(signature, target, values);
            break;
        }
        case FirCallKind::Contract: {
            if (values.empty() || call.contract >= program_.contracts.size() ||
                call.method >= program_.contracts[call.contract].methods.size()) {
                fail(span, "LLVM backend received an invalid contract call");
                return {};
            }
            const auto receiverType = function_->expressions[call.arguments.front()].type;
            auto *receiver = values.front();
            const auto boxed = receiverType.kind == TypeKind::Own;
            auto *contractType = contractTypes_[call.contract];
            if (boxed) {
                receiver = builder_.CreateLoad(contractType, receiver);
            }
            auto *data = builder_.CreateExtractValue(receiver, 0);
            auto *vtable = builder_.CreateExtractValue(receiver, 1);
            auto *methodAddress = builder_.CreateStructGEP(contractVtableTypes_[call.contract],
                                                           vtable, call.method + 1);
            auto *target = builder_.CreateLoad(pointerType(), methodAddress);
            const auto &method = program_.contracts[call.contract].methods[call.method];
            std::vector<llvm::Type *> parameterTypes{pointerType()};
            for (const auto &parameter : method.parameters) {
                parameterTypes.push_back(typeOf(parameter));
            }
            auto *signature =
                llvm::FunctionType::get(typeOf(method.returnType), parameterTypes, false);
            std::vector<llvm::Value *> methodArguments{data};
            methodArguments.insert(methodArguments.end(), values.begin() + 1, values.end());
            result = builder_.CreateCall(signature, target, methodArguments);
            if (contractCallConsumesReceiver(call)) {
                builder_.CreateCall(runtimeFunction("fdn_dealloc", llvm::Type::getVoidTy(context_),
                                                    {pointerType()}),
                                    {values.front()});
            }
            break;
        }
        }
        for (std::size_t index = 0; index < values.size() && index < call.argumentDrops.size();
             ++index) {
            if (call.argumentDrops[index]) {
                dropValue(values[index], function_->expressions[call.arguments[index]].type);
            }
        }
        return {result, false};
    }

    bool contractCallConsumesReceiver(const FirCallExpression &call) const {
        return call.kind == FirCallKind::Contract && call.contract < program_.contracts.size() &&
               call.method < program_.contracts[call.contract].methods.size() &&
               program_.contracts[call.contract].methods[call.method].receiver ==
                   FirReceiverKind::Own;
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

    EmittedValue emitConditional(const FirConditionalExpression &conditional, const Type &type) {
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
        if (local < captureAddresses_.size() && captureAddresses_[local] != nullptr) {
            auto *type = typeOf(function_->locals[local].type);
            return builder_.CreateLoad(type, captureAddresses_[local], "capture.value");
        }
        if (local >= locals_.size() || locals_[local] == nullptr) {
            fail(function_->sourceSpan, "LLVM backend received an invalid local");
            return nullptr;
        }
        return builder_.CreateLoad(locals_[local]->getAllocatedType(), locals_[local],
                                   "local.value");
    }

    bool isPlaceExpression(FirExpressionId id) const {
        if (id >= function_->expressions.size()) {
            return false;
        }
        const auto &expression = function_->expressions[id].value;
        if (std::holds_alternative<FirLocalExpression>(expression) ||
            std::holds_alternative<FirReadExpression>(expression)) {
            return true;
        }
        if (const auto *field = std::get_if<FirFieldExpression>(&expression)) {
            return isPlaceExpression(field->base);
        }
        if (const auto *unary = std::get_if<FirUnaryExpression>(&expression)) {
            return unary->operation == FirUnaryOperator::Dereference;
        }
        return false;
    }

    llvm::Value *emitAddress(FirExpressionId id) {
        if (id >= function_->expressions.size()) {
            return nullptr;
        }
        if (const auto *local =
                std::get_if<FirLocalExpression>(&function_->expressions[id].value)) {
            return localAddress(local->local);
        }
        if (const auto *field =
                std::get_if<FirFieldExpression>(&function_->expressions[id].value)) {
            return emitFieldAddress(*field);
        }
        if (const auto *index =
                std::get_if<FirIndexExpression>(&function_->expressions[id].value)) {
            return emitIndexAddress(*index, function_->expressions[id].span);
        }
        if (const auto *unary = std::get_if<FirUnaryExpression>(&function_->expressions[id].value);
            unary != nullptr && unary->operation == FirUnaryOperator::Dereference) {
            return emitExpression(unary->operand).value;
        }
        if (const auto *read = std::get_if<FirReadExpression>(&function_->expressions[id].value)) {
            return loadLocal(read->local);
        }
        if (const auto *field =
                std::get_if<FirFieldExpression>(&function_->expressions[id].value)) {
            auto baseType = function_->expressions[field->base].type;
            llvm::Value *base{};
            if (baseType.kind == TypeKind::Own || baseType.kind == TypeKind::View ||
                baseType.kind == TypeKind::Edit) {
                const auto emitted = emitExpression(field->base);
                if (emitted.diverges || baseType.arguments.size() != 1) {
                    return nullptr;
                }
                base = emitted.value;
                baseType = baseType.arguments.front();
            } else {
                base = emitAddress(field->base);
            }
            return base == nullptr ? nullptr
                                   : structFieldAddress(base, baseType, field->field,
                                                        function_->expressions[id].span);
        }
        if (const auto *unary = std::get_if<FirUnaryExpression>(&function_->expressions[id].value);
            unary != nullptr && unary->operation == FirUnaryOperator::Dereference) {
            const auto operand = emitExpression(unary->operand);
            return operand.diverges ? nullptr : operand.value;
        }
        fail(function_->expressions[id].span,
             "LLVM backend has not lowered this assignment target yet");
        return nullptr;
    }

    llvm::Value *valueAddress(llvm::Value *value, const Type &type, std::string_view name) {
        auto *layout = typeOf(type);
        if (value == nullptr || layout == nullptr || layout->isVoidTy()) {
            return nullptr;
        }
        auto *storage =
            builder_.CreateAlloca(layout, nullptr, llvm::StringRef(name.data(), name.size()));
        builder_.CreateStore(value, storage);
        return storage;
    }

    std::size_t structFieldIndex(FirStructId type, FirFieldId field) const {
        return field +
               (type < program_.structs.size() && program_.structs[type].dropFunction.has_value()
                    ? 1
                    : 0);
    }

    llvm::Value *structFieldAddress(llvm::Value *address, const Type &type, FirFieldId field,
                                    SourceSpan span) {
        if (type.kind != TypeKind::Struct || type.declaration >= program_.structs.size() ||
            field >= program_.structs[type.declaration].fields.size()) {
            fail(span, "LLVM backend received an invalid struct field address");
            return nullptr;
        }
        return builder_.CreateStructGEP(structTypes_[type.declaration], address,
                                        structFieldIndex(type.declaration, field), "field.address");
    }

    llvm::Value *enumTag(llvm::Value *address, const Type &type, SourceSpan span) {
        if (type.kind != TypeKind::Enum || type.declaration >= enumTypes_.size()) {
            fail(span, "LLVM backend received an invalid enum value");
            return nullptr;
        }
        auto *tag =
            builder_.CreateStructGEP(enumTypes_[type.declaration], address, 0, "enum.tag.address");
        return builder_.CreateLoad(llvm::Type::getInt32Ty(context_), tag, "enum.tag");
    }

    llvm::Value *enumPayloadAddress(llvm::Value *address, const Type &type, FirVariantId variant,
                                    SourceSpan span) {
        if (type.kind != TypeKind::Enum || type.declaration >= program_.enums.size() ||
            variant >= program_.enums[type.declaration].variants.size() ||
            !program_.enums[type.declaration].variants[variant].payload.has_value()) {
            fail(span, "LLVM backend received an invalid enum payload");
            return nullptr;
        }
        return builder_.CreateStructGEP(enumTypes_[type.declaration], address, variant + 1,
                                        "enum.payload.address");
    }

    bool bindEnumPayload(llvm::Value *address, const Type &type, FirVariantId variant,
                         FirLocalId local, bool consume, SourceSpan span) {
        if (local >= locals_.size() || local >= function_->locals.size()) {
            fail(span, "LLVM backend received an invalid enum binding");
            return false;
        }
        auto *payload = enumPayloadAddress(address, type, variant, span);
        if (payload == nullptr) {
            return false;
        }
        const auto &payloadType = *program_.enums[type.declaration].variants[variant].payload;
        const auto &localType = function_->locals[local].type;
        llvm::Value *value{};
        if ((localType.kind == TypeKind::View || localType.kind == TypeKind::Edit) &&
            payloadType.kind != TypeKind::Own && payloadType.kind != TypeKind::View &&
            payloadType.kind != TypeKind::Edit) {
            value = payload;
        } else if (consume && typeRequiresDrop(localType)) {
            value = moveFromAddress(payload, localType);
        } else {
            auto *layout = typeOf(localType);
            if (layout == nullptr || layout->isVoidTy()) {
                fail(span, "LLVM backend cannot lay out an enum binding");
                return false;
            }
            value = builder_.CreateLoad(layout, payload, "enum.binding");
        }
        builder_.CreateStore(value, locals_[local]);
        return true;
    }

    llvm::Value *emitEqual(llvm::Value *left, llvm::Value *right, const Type &type,
                           SourceSpan span) {
        if (left == nullptr || right == nullptr) {
            return nullptr;
        }
        if (type == stringType) {
            return builder_.CreateICmpNE(
                builder_.CreateCall(runtimeFunction("fdn_abi_string_equal",
                                                    llvm::Type::getInt32Ty(context_),
                                                    {pointerType(), pointerType()}),
                                    {stringAddress(left), stringAddress(right)}),
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0));
        }
        if (left->getType()->isFloatingPointTy()) {
            return builder_.CreateFCmpOEQ(left, right);
        }
        if (left->getType()->isIntegerTy() || left->getType()->isPointerTy()) {
            return builder_.CreateICmpEQ(left, right);
        }
        fail(span, "LLVM backend has not lowered equality for this pattern type yet");
        return nullptr;
    }

    llvm::Value *moveFromAddress(llvm::Value *address, const Type &type) {
        auto *layout = typeOf(type);
        if (address == nullptr || layout == nullptr || layout->isVoidTy()) {
            return nullptr;
        }
        auto *value = builder_.CreateLoad(layout, address, "move.value");
        builder_.CreateStore(llvm::Constant::getNullValue(layout), address);
        return value;
    }

    bool typeRequiresDrop(const Type &type) const {
        if (type.kind == TypeKind::String || type.kind == TypeKind::Own ||
            type.kind == TypeKind::Task || type.kind == TypeKind::Channel ||
            type.kind == TypeKind::Sender || type.kind == TypeKind::Receiver ||
            type.kind == TypeKind::Function || type.kind == TypeKind::Parameter) {
            return true;
        }
        if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
            return typeRequiresDrop(type.arguments.front());
        }
        if (type.kind == TypeKind::Struct && type.declaration < program_.structs.size()) {
            const auto &declaration = program_.structs[type.declaration];
            return declaration.dropFunction.has_value() ||
                   std::any_of(
                       declaration.fields.begin(), declaration.fields.end(),
                       [&](const FirStructField &field) { return typeRequiresDrop(field.type); });
        }
        if (type.kind == TypeKind::Enum && type.declaration < program_.enums.size()) {
            return std::any_of(program_.enums[type.declaration].variants.begin(),
                               program_.enums[type.declaration].variants.end(),
                               [&](const FirEnumVariant &variant) {
                                   return variant.payload.has_value() &&
                                          typeRequiresDrop(*variant.payload);
                               });
        }
        return false;
    }

    void dropMatchValue(llvm::Value *storage, llvm::Value *value, const Type &type) {
        if (type.kind == TypeKind::View || type.kind == TypeKind::Edit) {
            return;
        }
        if (type.kind == TypeKind::Own) {
            dropValue(value, type);
            return;
        }
        dropAddress(storage, type);
    std::size_t structFieldIndex(const Type &type, FirFieldId field) const {
        return field + (type.kind == TypeKind::Struct &&
                                type.declaration < program_.structs.size() &&
                                program_.structs[type.declaration].dropFunction.has_value()
                            ? 1
                            : 0);
    }

    llvm::Value *emitFieldAddress(const FirFieldExpression &field) {
        if (field.base >= function_->expressions.size()) {
            return nullptr;
        }
        auto baseType = function_->expressions[field.base].type;
        llvm::Value *base{};
        if (baseType.kind == TypeKind::Own || baseType.kind == TypeKind::View ||
            baseType.kind == TypeKind::Edit) {
            base = emitExpression(field.base).value;
            if (baseType.arguments.size() != 1) {
                return nullptr;
            }
            baseType = baseType.arguments.front();
        } else {
            base = emitAddress(field.base);
        }
        auto *type = llvm::dyn_cast_or_null<llvm::StructType>(typeOf(baseType));
        if (base == nullptr || type == nullptr) {
            fail(function_->expressions[field.base].span, "LLVM backend cannot address this field");
            return nullptr;
        }
        return builder_.CreateStructGEP(type, base, structFieldIndex(baseType, field.field));
    }

    llvm::Value *emitIndexAddress(const FirIndexExpression &index, SourceSpan span) {
        if (index.base >= function_->expressions.size()) {
            return nullptr;
        }
        const auto value = emitExpression(index.index);
        if (value.diverges) {
            return nullptr;
        }
        auto sequence = function_->expressions[index.base].type;
        llvm::Value *data{};
        llvm::Value *length{};
        if (sequence.kind == TypeKind::Own && sequence.arguments.size() == 1) {
            data = emitExpression(index.base).value;
            sequence = sequence.arguments.front();
        } else if ((sequence.kind == TypeKind::View || sequence.kind == TypeKind::Edit) &&
                   sequence.arguments.size() == 1 &&
                   sequence.arguments.front().kind == TypeKind::Slice) {
            auto slice = emitExpression(index.base).value;
            sequence = sequence.arguments.front();
            data = builder_.CreateExtractValue(slice, 0);
            length = builder_.CreateExtractValue(slice, 1);
        } else {
            data = emitAddress(index.base);
        }
        if (sequence.kind == TypeKind::Array && sequence.arguments.size() == 1) {
            length = llvm::ConstantInt::get(sizeType(), sequence.declaration);
            auto *checked = builder_.CreateCall(
                runtimeFunction("fdn_bounds_check", sizeType(), {sizeType(), sizeType()}),
                {integerToSize(value.value), length});
            auto *array = llvm::cast<llvm::ArrayType>(typeOf(sequence));
            return builder_.CreateInBoundsGEP(
                array, data,
                {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0), checked});
        }
        if (sequence.kind == TypeKind::Slice && sequence.arguments.size() == 1) {
            auto *checked = builder_.CreateCall(
                runtimeFunction("fdn_bounds_check", sizeType(), {sizeType(), sizeType()}),
                {integerToSize(value.value), length});
            return builder_.CreateInBoundsGEP(typeOf(sequence.arguments.front()), data, checked);
        }
        fail(span, "LLVM backend cannot index this value");
        return nullptr;
    }

    EmittedValue emitRawPointer(const FirRawPointerExpression &pointer, SourceSpan span) {
        if (pointer.base >= function_->expressions.size()) {
            return {};
        }
        auto type = function_->expressions[pointer.base].type;
        const auto base = emitExpression(pointer.base);
        if (base.diverges) {
            return base;
        }
        if ((type.kind == TypeKind::View || type.kind == TypeKind::Edit) &&
            type.arguments.size() == 1 && type.arguments.front().kind == TypeKind::Slice) {
            return {builder_.CreateExtractValue(base.value, 0), false};
        }
        if (type.kind == TypeKind::Slice) {
            return {builder_.CreateExtractValue(base.value, 0), false};
        }
        fail(span, "LLVM backend cannot expose a raw pointer for this value");
        return {};
    }

    llvm::Value *integerToSize(llvm::Value *value) {
        return value->getType() == sizeType() ? value
                                              : builder_.CreateIntCast(value, sizeType(), false);
    }

    llvm::Value *localAddress(FirLocalId local) const {
        if (local < captureAddresses_.size() && captureAddresses_[local] != nullptr) {
            return captureAddresses_[local];
        }
        return local < locals_.size() ? locals_[local] : nullptr;
    }

    llvm::FunctionCallee runtimeFunction(std::string_view name, llvm::Type *result,
                                         std::vector<llvm::Type *> parameters) {
        return module_.getOrInsertFunction(llvm::StringRef(name.data(), name.size()),
                                           llvm::FunctionType::get(result, parameters, false));
    }

    llvm::Value *stringAddress(llvm::Value *value) {
        auto *storage = builder_.CreateAlloca(stringType_, nullptr, "string.abi");
        builder_.CreateStore(value, storage);
        return storage;
    }

    void enterFrame() {
        const auto source =
            function_->sourcePath.empty() ? sourcePath_ : std::string_view(function_->sourcePath);
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
                             llvm::Type::getInt32Ty(context_), llvm::Type::getInt32Ty(context_)}),
            {frame_, packageName, functionNameValue, sourceName,
             llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), function_->sourceSpan.line),
             llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_),
                                    function_->sourceSpan.column)});
    }

    void leaveFrame() {
        builder_.CreateCall(
            runtimeFunction("fdn_frame_leave", llvm::Type::getVoidTy(context_), {pointerType()}),
            {frame_});
    }

    void setLocation(SourceSpan span) {
        if (frame_ == nullptr || builder_.GetInsertBlock() == nullptr ||
            builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        auto *line = builder_.CreateStructGEP(frameType_, frame_, 4);
        auto *column = builder_.CreateStructGEP(frameType_, frame_, 5);
        builder_.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), span.line),
                             line);
        builder_.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), span.column),
                             column);
    }

    void dropOwned(llvm::Value *value, const Type &type) {
        if (value == nullptr || type.arguments.size() != 1) {
            return;
        }
        auto *drop = llvm::BasicBlock::Create(context_, "drop.own", llvmFunction_);
        auto *done = llvm::BasicBlock::Create(context_, "drop.own.end", llvmFunction_);
        builder_.CreateCondBr(
            builder_.CreateICmpNE(value, llvm::ConstantPointerNull::get(pointerType())), drop,
            done);
        builder_.SetInsertPoint(drop);
        const auto &target = type.arguments.front();
        if (target.kind == TypeKind::Contract &&
            target.declaration < contractVtableTypes_.size()) {
            auto *contract = builder_.CreateLoad(typeOf(target), value, "owned.contract");
            auto *data = builder_.CreateExtractValue(contract, 0);
            auto *vtable = builder_.CreateExtractValue(contract, 1);
            auto *dropSlot =
                builder_.CreateStructGEP(contractVtableTypes_[target.declaration], vtable, 0);
            auto *dropFunction = builder_.CreateLoad(pointerType(), dropSlot);
            auto *signature =
                llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {pointerType()}, false);
            builder_.CreateCall(signature, dropFunction, {data});
        } else {
            dropAddress(value, target);
        }
        builder_.CreateCall(
            runtimeFunction("fdn_dealloc", llvm::Type::getVoidTy(context_), {pointerType()}),
            {value});
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
    }

    void dropAddress(llvm::Value *address, const Type &type) {
        if (address == nullptr || !typeRequiresDrop(type) ||
            builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        if (type == stringType) {
            builder_.CreateCall(runtimeFunction("fdn_string_drop", llvm::Type::getVoidTy(context_),
                                                {pointerType()}),
                                {address});
            return;
        }
        if (type.kind == TypeKind::Function) {
            auto *value = builder_.CreateLoad(functionValueType_, address, "drop.function");
            auto *environment = builder_.CreateExtractValue(value, 0);
            auto *dropFunction = builder_.CreateExtractValue(value, 2);
            auto *drop = llvm::BasicBlock::Create(context_, "drop.closure", llvmFunction_);
            auto *done = llvm::BasicBlock::Create(context_, "drop.closure.end", llvmFunction_);
            builder_.CreateCondBr(
                builder_.CreateICmpNE(dropFunction,
                                      llvm::ConstantPointerNull::get(pointerType())),
                drop, done);
            builder_.SetInsertPoint(drop);
            auto *signature =
                llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {pointerType()}, false);
            builder_.CreateCall(signature, dropFunction, {environment});
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
            builder_.CreateStore(llvm::Constant::getNullValue(functionValueType_), address);
            return;
        }
        }
        if (type.kind == TypeKind::Own && type.arguments.size() == 1) {
            auto *value = builder_.CreateLoad(pointerType(), address, "drop.own.value");
            dropOwned(value, type);
            builder_.CreateStore(llvm::ConstantPointerNull::get(pointerType()), address);
            return;
        }
        if (type.kind == TypeKind::Sender || type.kind == TypeKind::Receiver) {
            builder_.CreateCall(runtimeFunction(type.kind == TypeKind::Sender
                                                    ? "fdn_channel_drop_sender"
                                                    : "fdn_channel_drop_receiver",
                                                llvm::Type::getVoidTy(context_), {pointerType()}),
                                {address});
            return;
        }
        if (type.kind == TypeKind::Channel) {
            auto *receiver =
                builder_.CreateStructGEP(channelType_, address, 1, "channel.receiver.address");
            auto *sender =
                builder_.CreateStructGEP(channelType_, address, 0, "channel.sender.address");
            dropAddress(receiver, Type{TypeKind::Receiver});
            dropAddress(sender, Type{TypeKind::Sender});
            builder_.CreateStore(llvm::Constant::getNullValue(channelType_), address);
            return;
        }
        if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
            auto *array = llvm::dyn_cast_or_null<llvm::ArrayType>(typeOf(type));
            if (array == nullptr) {
                fail(function_->sourceSpan, "LLVM backend cannot drop this array value");
                return;
            }
            for (std::size_t index = type.declaration; index-- > 0;) {
                auto *element = builder_.CreateInBoundsGEP(
                    array, address,
                    {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0),
                     llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), index)});
                dropAddress(element, type.arguments.front());
            }
            builder_.CreateStore(llvm::Constant::getNullValue(array), address);
            return;
        }
        if (type.kind == TypeKind::Struct && type.declaration < program_.structs.size()) {
            const auto &declaration = program_.structs[type.declaration];
            if (declaration.dropFunction.has_value()) {
                auto *active = builder_.CreateStructGEP(structTypes_[type.declaration], address, 0,
                                                        "drop.active.address");
                auto *drop = llvm::BasicBlock::Create(context_, "drop.struct", llvmFunction_);
                auto *done = llvm::BasicBlock::Create(context_, "drop.struct.end", llvmFunction_);
                builder_.CreateCondBr(
                    builder_.CreateLoad(llvm::Type::getInt1Ty(context_), active, "drop.active"),
                    drop, done);
                builder_.SetInsertPoint(drop);
                builder_.CreateStore(llvm::ConstantInt::getFalse(context_), active);
                const auto function = *declaration.dropFunction;
                if (function >= functions_.size() || functions_[function] == nullptr) {
                    fail(declaration.sourceSpan,
                         "LLVM backend received an invalid struct drop function");
                } else {
                    builder_.CreateCall(functions_[function], {address});
                }
                for (std::size_t field = declaration.fields.size(); field-- > 0;) {
                    dropAddress(structFieldAddress(address, type, field, declaration.sourceSpan),
                                declaration.fields[field].type);
                }
                builder_.CreateStore(llvm::Constant::getNullValue(structTypes_[type.declaration]),
                                     address);
                builder_.CreateBr(done);
                builder_.SetInsertPoint(done);
                return;
            }
            for (std::size_t field = declaration.fields.size(); field-- > 0;) {
                dropAddress(structFieldAddress(address, type, field, declaration.sourceSpan),
                            declaration.fields[field].type);
            }
            builder_.CreateStore(llvm::Constant::getNullValue(structTypes_[type.declaration]),
                                 address);
            return;
        }
        if (type.kind == TypeKind::Enum && type.declaration < program_.enums.size()) {
            const auto &declaration = program_.enums[type.declaration];
            auto *tag = enumTag(address, type, function_->sourceSpan);
            if (tag == nullptr) {
                return;
            }
            auto *done = llvm::BasicBlock::Create(context_, "drop.enum.end", llvmFunction_);
            auto *invalid = llvm::BasicBlock::Create(context_, "drop.enum.invalid", llvmFunction_);
            auto *selection = builder_.CreateSwitch(tag, invalid, declaration.variants.size());
            for (std::size_t variant = 0; variant < declaration.variants.size(); ++variant) {
                auto *branch = llvm::BasicBlock::Create(context_, "drop.enum", llvmFunction_);
                selection->addCase(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), variant), branch);
                builder_.SetInsertPoint(branch);
                const auto &payload = declaration.variants[variant].payload;
                if (payload.has_value() && typeRequiresDrop(*payload)) {
                    dropAddress(enumPayloadAddress(address, type, variant, function_->sourceSpan),
                                *payload);
                }
                builder_.CreateBr(done);
            }
            builder_.SetInsertPoint(invalid);
            builder_.CreateCall(
                runtimeFunction("fdn_invalid_enum_tag", llvm::Type::getVoidTy(context_), {}));
            builder_.CreateUnreachable();
            builder_.SetInsertPoint(done);
            builder_.CreateStore(llvm::Constant::getNullValue(enumTypes_[type.declaration]),
                                 address);
            return;
        }
        fail(function_->sourceSpan, "LLVM backend has not lowered drop for this value type yet");
    }

    void dropValue(llvm::Value *value, const Type &type) {
        if (value == nullptr || !typeRequiresDrop(type) ||
            builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        dropAddress(valueAddress(value, type, "drop.value"), type);
    }

    void dropLocals(const std::vector<FirLocalId> &locals) {
        for (const auto local : locals) {
            if (local >= function_->locals.size() || local >= locals_.size()) {
                continue;
            }
            dropAddress(locals_[local], function_->locals[local].type);
        }
    }

    llvm::Value *toAbi(llvm::IRBuilder<> &builder, llvm::Value *value, const Type &type) {
        static_cast<void>(builder);
        static_cast<void>(type);
        return value;
    }

    llvm::Value *fromAbi(llvm::IRBuilder<> &builder, llvm::Value *value, const Type &type) {
        static_cast<void>(builder);
        static_cast<void>(type);
        return value;
    }

    void enterNativeFrame(llvm::IRBuilder<> &builder, llvm::Value *frame,
                          const FirFunction &function) {
        const auto source =
            function.sourcePath.empty() ? sourcePath_ : std::string_view(function.sourcePath);
        auto *symbol = builder.CreateGlobalString(*function.cSymbol, "native.symbol");
        auto *sourceName = builder.CreateGlobalString(source, "native.source");
        builder.CreateCall(
            module_.getOrInsertFunction(
                "fdn_frame_enter_native",
                llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                        {pointerType(), pointerType(), pointerType(),
                                         llvm::Type::getInt32Ty(context_),
                                         llvm::Type::getInt32Ty(context_)},
                                        false)),
            {frame, symbol, sourceName,
             llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), function.sourceSpan.line),
             llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), function.sourceSpan.column)});
    }

    void emitNativeWrappers() {
        for (FirFunctionId id = 0; id < program_.functions.size(); ++id) {
            const auto &function = program_.functions[id];
            if (!function.cSymbol.has_value() || functions_[id] == nullptr ||
                nativeFunctions_[id] == nullptr) {
                continue;
            }
            auto *wrapper = function.hasBody ? nativeFunctions_[id] : functions_[id];
            auto *target = function.hasBody ? functions_[id] : nativeFunctions_[id];
            if (!wrapper->empty()) {
                continue;
            }
            auto *entry = llvm::BasicBlock::Create(context_, "entry", wrapper);
            llvm::IRBuilder<> builder(entry);
            auto *frame = builder.CreateAlloca(frameType_, nullptr, "frame");
            enterNativeFrame(builder, frame, function);
            std::vector<llvm::Value *> arguments;
            for (std::size_t index = 0; index < wrapper->arg_size(); ++index) {
                const auto local = function.parameters[index];
                auto *value = wrapper->getArg(index);
                arguments.push_back(function.hasBody
                                        ? fromAbi(builder, value, function.locals[local].type)
                                        : toAbi(builder, value, function.locals[local].type));
            }
            auto *result = builder.CreateCall(target, arguments);
            if (function.diverges) {
                builder.CreateUnreachable();
                continue;
            }
            builder.CreateCall(
                module_.getOrInsertFunction("fdn_frame_leave",
                                            llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                                                    {pointerType()}, false)),
                {frame});
            if (function.returnType == voidType) {
                builder.CreateRetVoid();
            } else {
                builder.CreateRet(function.hasBody ? toAbi(builder, result, function.returnType)
                                                   : fromAbi(builder, result, function.returnType));
            }
        }
    }

    void emitMainWrapper() {
        const auto &entry = program_.functions[program_.main];
        if (!entry.parameters.empty()) {
            fail(entry.sourceSpan, "LLVM backend has not lowered command-line arguments yet");
            return;
        }
        auto *signature = llvm::FunctionType::get(llvm::Type::getInt32Ty(context_), {}, false);
        auto *main =
            llvm::Function::Create(signature, llvm::GlobalValue::ExternalLinkage, "main", module_);
        auto *block = llvm::BasicBlock::Create(context_, "entry", main);
        builder_.SetInsertPoint(block);
        auto *entryFunction = functions_[program_.main];
        if (options_.entry.has_value()) {
            builder_.CreateCall(entryFunction);
            if (options_.verifyAllocations) {
                emitAllocationCheck();
            }
            builder_.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0));
            return;
        }
        auto *result = builder_.CreateCall(entryFunction);
        if (options_.verifyAllocations) {
            emitAllocationCheck();
        }
        builder_.CreateRet(result);
    }

    void emitAllocationCheck() {
        auto *count = builder_.CreateCall(runtimeFunction("fdn_live_allocations", sizeType(), {}));
        auto *clean = llvm::BasicBlock::Create(context_, "allocations.clean", mainFunction());
        auto *failed = llvm::BasicBlock::Create(context_, "allocations.failed", mainFunction());
        builder_.CreateCondBr(builder_.CreateICmpEQ(count, llvm::ConstantInt::get(sizeType(), 0)),
                              clean, failed);
        builder_.SetInsertPoint(failed);
        auto *message = builder_.CreateGlobalString("live allocations after entry point",
                                                    "allocations.message");
        builder_.CreateCall(
            runtimeFunction("fdn_panic_cstr", llvm::Type::getVoidTy(context_), {pointerType()}),
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
    llvm::StructType *channelType_{};
    llvm::StructType *functionValueType_{};
    std::vector<llvm::StructType *> structTypes_;
    std::vector<llvm::StructType *> enumTypes_;
    std::vector<llvm::StructType *> contractTypes_;
    std::vector<llvm::StructType *> contractVtableTypes_;
    std::vector<llvm::StructType *> closureEnvironmentTypes_;
    std::unordered_map<std::string, llvm::StructType *> sliceTypes_;
    std::vector<llvm::Function *> functions_;
    std::vector<llvm::Function *> nativeFunctions_;
    std::vector<llvm::Function *> functionAdapters_;
    std::vector<llvm::Function *> closureDrops_;
    std::map<std::string, ContractSupport> contractSupports_;
    const FirFunction *function_{};
    FirFunctionId functionId_{};
    llvm::Function *llvmFunction_{};
    llvm::AllocaInst *frame_{};
    std::vector<llvm::AllocaInst *> locals_;
    std::vector<llvm::Value *> captureAddresses_;
    std::vector<LoopTarget> loops_;
};

std::optional<LlvmModule> buildModule(const FirProgram &program, std::string_view sourcePath,
                                      const LlvmCodegenOptions &options, Diagnostics &diagnostics) {
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
    LlvmEmitter emitter(program, sourcePath, options, diagnostics, *result.context, *result.module);
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
        auto pipeline = passes.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
        pipeline.run(*result.module, modules);
    }
    return result;
}

} // namespace

std::string defaultLlvmTargetTriple() {
    return llvm::Triple::normalize(llvm::sys::getDefaultTargetTriple());
}

std::optional<std::string> emitLlvmIr(const FirProgram &program, std::string_view sourcePath,
                                      const LlvmCodegenOptions &options, Diagnostics &diagnostics) {
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
                    std::string_view sourcePath, const LlvmCodegenOptions &options,
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
    if (generated->target->addPassesToEmitFile(passes, object, nullptr,
                                               llvm::CodeGenFileType::ObjectFile)) {
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
