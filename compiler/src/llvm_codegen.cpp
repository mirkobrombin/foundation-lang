#include "foundation/llvm_codegen.hpp"

#include "foundation/codegen.hpp"

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DebugInfoMetadata.h>
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
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
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

struct EmittedCleanup {
    llvm::Value *address{};
    Type type{invalidType};
};

struct EmittedValue {
    llvm::Value *value{};
    bool diverges{};
    std::vector<EmittedCleanup> cleanups;

    EmittedValue(llvm::Value *emitted = nullptr, bool exits = false,
                 std::vector<EmittedCleanup> cleanupValues = {})
        : value(emitted), diverges(exits), cleanups(std::move(cleanupValues)) {}
};

FirBinaryOperator assignmentBinary(FirAssignmentOperator operation) {
    switch (operation) {
    case FirAssignmentOperator::Add:
        return FirBinaryOperator::Add;
    case FirAssignmentOperator::Subtract:
        return FirBinaryOperator::Subtract;
    case FirAssignmentOperator::Multiply:
        return FirBinaryOperator::Multiply;
    case FirAssignmentOperator::Divide:
        return FirBinaryOperator::Divide;
    case FirAssignmentOperator::Remainder:
        return FirBinaryOperator::Remainder;
    case FirAssignmentOperator::ShiftLeft:
        return FirBinaryOperator::ShiftLeft;
    case FirAssignmentOperator::ShiftRight:
        return FirBinaryOperator::ShiftRight;
    case FirAssignmentOperator::Assign:
        break;
    }
    std::terminate();
}

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

unsigned int llvmIndex(std::size_t value) {
    if (value > std::numeric_limits<unsigned int>::max()) {
        std::terminate();
    }
    return static_cast<unsigned int>(value);
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
        : program_(options.libraryPackage.has_value()
                       ? specializePackageInterface(source, *options.libraryPackage)
                       : prepareFirForBackend(source, options.entry)),
          sourcePath_(sourcePath),
          options_(options), diagnostics_(diagnostics), context_(context), module_(module),
          builder_(context) {
        if (options_.libraryPackage.has_value()) {
            program_.main = program_.functions.size();
        }
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
        selectCaseType_ = llvm::StructType::create(
            context_, {pointerType(), pointerType(), llvm::Type::getInt32Ty(context_)},
            "fdn.channel.select.case");
        initializeDebugInfo();
    }

    bool run() {
        if (!options_.libraryPackage.has_value() &&
            program_.main >= program_.functions.size()) {
            fail({}, "program has no LLVM entry point");
            return false;
        }
        prepareTypes();
        if (diagnostics_.hasErrors()) {
            return false;
        }
        declareChannelDrops();
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
            emitChannelDrops();
            emitTaskAdapters();
            emitDropHelpers();
        }
        if (!diagnostics_.hasErrors() && !options_.libraryPackage.has_value()) {
            emitMainWrapper();
        }
        if (!diagnostics_.hasErrors() && debugBuilder_ != nullptr) {
            debugBuilder_->finalize();
        }
        return !diagnostics_.hasErrors();
    }

  private:
    struct LoopTarget {
        llvm::BasicBlock *breakBlock{};
        llvm::BasicBlock *continueBlock{};
    };

    struct DefaultContractSupport {
        Type contract{invalidType};
        std::vector<std::size_t> effectiveMethods;
        std::vector<llvm::Function *> methods;
        llvm::GlobalVariable *vtable{};
    };

    struct ContractSupport {
        Type contract{invalidType};
        Type concrete{invalidType};
        std::vector<FirContractMethodTarget> targets;
        llvm::Function *drop{};
        std::vector<llvm::Function *> methods;
        std::vector<std::optional<DefaultContractSupport>> defaults;
        llvm::GlobalVariable *vtable{};
    };

    struct ChannelDrop {
        Type type{invalidType};
        FirFunctionId owner{};
        llvm::Function *function{};
    };

    struct DropHelper {
        Type type{invalidType};
        FirFunctionId owner{};
        llvm::Function *function{};
    };

    struct TaskAdapter {
        llvm::StructType *frame{};
        llvm::Function *poll{};
        llvm::Function *moveResult{};
        llvm::Function *dropFrame{};
        std::vector<std::size_t> argumentFields;
        std::size_t argumentsActiveField{};
        std::size_t stateField{};
        std::optional<std::size_t> resultField;
        std::optional<std::size_t> resultActiveField;
        std::vector<std::size_t> localFields;
        std::vector<std::optional<std::size_t>> localActiveFields;
        std::vector<std::optional<std::size_t>> blockingFields;
        std::vector<std::optional<std::size_t>> callbackFields;
        std::vector<std::optional<std::size_t>> expressionStates;
        std::vector<std::optional<std::size_t>> statementStates;
        std::vector<llvm::Function *> blockingWorkers;
        std::vector<llvm::Function *> callbackStarts;
        std::vector<llvm::Function *> callbackCancels;
    };

    llvm::PointerType *pointerType() const { return llvm::PointerType::get(context_, 0); }

    llvm::IntegerType *sizeType() const {
        const auto bits = module_.getDataLayout().getPointerSizeInBits();
        return llvm::IntegerType::get(context_, bits == 0 ? 64 : bits);
    }

    void initializeDebugInfo() {
        if (!options_.debugInfo) {
            return;
        }
        debugBuilder_ = std::make_unique<llvm::DIBuilder>(module_);
        const auto path = !options_.sourcePaths.empty()
                              ? std::filesystem::path(options_.sourcePaths.front())
                              : sourcePath_.empty() ? std::filesystem::path("program.fn")
                                                    : std::filesystem::path(sourcePath_);
        primaryDebugFile_ = debugFile(path.generic_string());
        const llvm::Triple triple(module_.getTargetTriple());
        module_.addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                              llvm::DEBUG_METADATA_VERSION);
        if (triple.isOSWindows()) {
            module_.addModuleFlag(llvm::Module::Warning, "CodeView", 1);
        } else {
            module_.addModuleFlag(llvm::Module::Warning, "Dwarf Version", 5);
        }
        debugBuilder_->createCompileUnit(
            llvm::dwarf::DW_LANG_C11, primaryDebugFile_, "Foundation Lang 0.1.0",
            options_.optimize, "", 0);
    }

    llvm::DIFile *debugFile(std::string_view source) {
        auto path = source.empty() ? std::filesystem::path("program.fn")
                                   : std::filesystem::path(source);
        const auto key = path.lexically_normal().generic_string();
        if (const auto existing = debugFiles_.find(key); existing != debugFiles_.end()) {
            return existing->second;
        }
        auto directory = path.parent_path().generic_string();
        if (directory.empty()) {
            directory = ".";
        }
        auto filename = path.filename().generic_string();
        if (filename.empty()) {
            filename = "program.fn";
        }
        auto *file = debugBuilder_->createFile(filename, directory);
        debugFiles_.emplace(key, file);
        return file;
    }

    llvm::DIFile *debugFile(const FirFunction &function) {
        if (function.sourceSpan.source < options_.sourcePaths.size()) {
            return debugFile(options_.sourcePaths[function.sourceSpan.source]);
        }
        return debugFile(function.sourcePath.empty() ? sourcePath_ : function.sourcePath);
    }

    std::string debugTypeName(const Type &type) const {
        switch (type.kind) {
        case TypeKind::Struct:
            if (type.declaration < program_.structs.size()) {
                return program_.structs[type.declaration].name;
            }
            break;
        case TypeKind::Enum:
            if (type.declaration < program_.enums.size()) {
                return program_.enums[type.declaration].name;
            }
            break;
        case TypeKind::Contract:
            if (type.declaration < program_.contracts.size()) {
                return program_.contracts[type.declaration].name;
            }
            break;
        default:
            break;
        }
        return typeName(type);
    }

    llvm::DIType *debugType(const Type &type) {
        if (debugBuilder_ == nullptr || type.kind == TypeKind::Void ||
            type.kind == TypeKind::Never) {
            return nullptr;
        }
        auto *llvmType = typeOf(type);
        const auto bits = llvmType == nullptr
                              ? 0
                              : module_.getDataLayout()
                                    .getTypeSizeInBits(llvmType)
                                    .getFixedValue();
        if (isSignedInteger(type)) {
            return debugBuilder_->createBasicType(debugTypeName(type), bits,
                                                  llvm::dwarf::DW_ATE_signed);
        }
        if (isUnsignedInteger(type)) {
            return debugBuilder_->createBasicType(debugTypeName(type), bits,
                                                  llvm::dwarf::DW_ATE_unsigned);
        }
        if (isFloating(type)) {
            return debugBuilder_->createBasicType(debugTypeName(type), bits,
                                                  llvm::dwarf::DW_ATE_float);
        }
        if (type.kind == TypeKind::Bool) {
            return debugBuilder_->createBasicType("bool", bits,
                                                  llvm::dwarf::DW_ATE_boolean);
        }
        if (type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst ||
            type.kind == TypeKind::Own || type.kind == TypeKind::View ||
            type.kind == TypeKind::Edit || type.kind == TypeKind::Task ||
            type.kind == TypeKind::Sender || type.kind == TypeKind::Receiver) {
            auto *target = type.arguments.size() == 1 ? debugType(type.arguments.front())
                                                     : nullptr;
            return debugBuilder_->createPointerType(
                target, module_.getDataLayout().getPointerSizeInBits(), 0, std::nullopt,
                debugTypeName(type));
        }
        return debugBuilder_->createUnspecifiedType(debugTypeName(type));
    }

    llvm::DISubroutineType *debugFunctionType(const FirFunction &function) {
        std::vector<llvm::Metadata *> types;
        types.reserve(function.parameters.size() + 1);
        types.push_back(debugType(function.returnType));
        for (const auto local : function.parameters) {
            types.push_back(local < function.locals.size() ? debugType(function.locals[local].type)
                                                            : nullptr);
        }
        return debugBuilder_->createSubroutineType(debugBuilder_->getOrCreateTypeArray(types));
    }

    void attachFunctionDebugInfo(const FirFunction &function) {
        if (debugBuilder_ == nullptr || llvmFunction_ == nullptr) {
            return;
        }
        auto *file = debugFile(function);
        currentSubprogram_ = debugBuilder_->createFunction(
            file, traceFunctionName(function), llvmFunction_->getName(), file,
            static_cast<unsigned>(function.sourceSpan.line), debugFunctionType(function),
            static_cast<unsigned>(function.sourceSpan.line), llvm::DINode::FlagPrototyped,
            llvm::DISubprogram::SPFlagDefinition |
                (options_.optimize ? llvm::DISubprogram::SPFlagOptimized
                                   : llvm::DISubprogram::SPFlagZero));
        llvmFunction_->setSubprogram(currentSubprogram_);
        builder_.SetCurrentDebugLocation(llvm::DILocation::get(
            context_, static_cast<unsigned>(function.sourceSpan.line),
            static_cast<unsigned>(function.sourceSpan.column), currentSubprogram_));
    }

    void declareLocalDebugInfo(FirLocalId local) {
        if (debugBuilder_ == nullptr || currentSubprogram_ == nullptr ||
            local >= function_->locals.size() || local >= locals_.size() ||
            locals_[local] == nullptr) {
            return;
        }
        const auto &source = function_->locals[local];
        if (source.name.empty()) {
            return;
        }
        const auto parameter =
            std::find(function_->parameters.begin(), function_->parameters.end(), local);
        llvm::DILocalVariable *variable{};
        auto *file = debugFile(*function_);
        if (parameter != function_->parameters.end()) {
            variable = debugBuilder_->createParameterVariable(
                currentSubprogram_, source.name,
                static_cast<unsigned>(std::distance(function_->parameters.begin(), parameter) +
                                      1),
                file, static_cast<unsigned>(function_->sourceSpan.line), debugType(source.type),
                true);
        } else {
            variable = debugBuilder_->createAutoVariable(
                currentSubprogram_, source.name, file,
                static_cast<unsigned>(function_->sourceSpan.line), debugType(source.type), true);
        }
        debugBuilder_->insertDeclare(
            locals_[local], variable, debugBuilder_->createExpression(),
            llvm::DILocation::get(context_, static_cast<unsigned>(function_->sourceSpan.line),
                                  static_cast<unsigned>(function_->sourceSpan.column),
                                  currentSubprogram_),
            builder_.GetInsertBlock());
    }

    void clearDebugLocation() {
        builder_.SetCurrentDebugLocation({});
        currentSubprogram_ = nullptr;
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
            if (isCFunction(type))
                return pointerType();
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
        std::vector<llvm::Type *> parameters;
        if (!isCFunction(type)) {
            parameters.push_back(pointerType());
        }
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
                        isCFunction(expression.type) ||
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
        support.defaults.resize(contract.methods.size());
        for (std::size_t method = 0; method < contract.methods.size(); ++method) {
            const auto &target = support.targets[method];
            if (!target.contractDefault) {
                continue;
            }
            if (target.defaultContract.kind != TypeKind::Contract ||
                target.defaultContract.declaration >= program_.contracts.size()) {
                fail(span, "LLVM default method has an invalid origin contract");
                return;
            }
            DefaultContractSupport self;
            self.contract = target.defaultContract;
            const auto &origin = program_.contracts[target.defaultContract.declaration];
            for (std::size_t originMethod = 0; originMethod < origin.methods.size();
                 ++originMethod) {
                const auto found = std::find_if(
                    contract.methods.begin(), contract.methods.end(), [&](const auto &candidate) {
                        return candidate.name == origin.methods[originMethod].name;
                    });
                if (found == contract.methods.end()) {
                    fail(span, "LLVM default method is missing an effective contract method");
                    return;
                }
                self.effectiveMethods.push_back(
                    static_cast<std::size_t>(found - contract.methods.begin()));
                std::vector<llvm::Type *> parameters{pointerType()};
                for (const auto &parameter : origin.methods[originMethod].parameters) {
                    auto *type = typeOf(parameter);
                    if (type == nullptr || type->isVoidTy()) {
                        fail(span, "LLVM default method has an unsupported parameter");
                        return;
                    }
                    parameters.push_back(type);
                }
                auto *result = typeOf(origin.methods[originMethod].returnType);
                if (result == nullptr) {
                    fail(span, "LLVM default method has an unsupported result");
                    return;
                }
                self.methods.push_back(llvm::Function::Create(
                    llvm::FunctionType::get(result, parameters, false),
                    llvm::GlobalValue::InternalLinkage,
                    vtableName(support.contract, support.concrete) + "_m" +
                        std::to_string(method) + "_self_m" + std::to_string(originMethod),
                    module_));
            }
            std::vector<llvm::Constant *> entries{support.drop};
            entries.insert(entries.end(), self.methods.begin(), self.methods.end());
            self.vtable = new llvm::GlobalVariable(
                module_, contractVtableTypes_[target.defaultContract.declaration], true,
                llvm::GlobalValue::InternalLinkage,
                llvm::ConstantStruct::get(
                    contractVtableTypes_[target.defaultContract.declaration], entries),
                vtableName(support.contract, support.concrete) + "_m" +
                    std::to_string(method) + "_self");
            support.defaults[method] = std::move(self);
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
            auto *field = builder_.CreateStructGEP(closureEnvironmentTypes_[id], environment,
                                                   llvmIndex(captureIndex++));
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
        for (const auto &defaultSupport : support.defaults) {
            if (!defaultSupport.has_value()) {
                continue;
            }
            const auto &origin = program_.contracts[defaultSupport->contract.declaration];
            for (std::size_t method = 0; method < defaultSupport->methods.size(); ++method) {
                auto *adapter = defaultSupport->methods[method];
                auto *block = llvm::BasicBlock::Create(context_, "entry", adapter);
                llvm::IRBuilder<> builder(block);
                std::vector<llvm::Value *> arguments;
                for (auto &argument : adapter->args()) {
                    arguments.push_back(&argument);
                }
                auto *call =
                    builder.CreateCall(support.methods[defaultSupport->effectiveMethods[method]],
                                       arguments);
                if (origin.methods[method].returnType == voidType) {
                    builder.CreateRetVoid();
                } else {
                    builder.CreateRet(call);
                }
            }
        }
        for (std::size_t method = 0; method < support.methods.size(); ++method) {
            auto *adapter = support.methods[method];
            auto *block = llvm::BasicBlock::Create(context_, "entry", adapter);
            llvm::IRBuilder<> builder(block);
            const auto target = support.targets[method].function;
            if (target >= program_.functions.size() || functions_[target] == nullptr) {
                fail({}, "LLVM contract adapter has an invalid implementation");
                return;
            }
            llvm::Value *receiver = adapter->getArg(0);
            if (support.targets[method].contractDefault) {
                const auto &self = support.defaults[method];
                if (!self.has_value()) {
                    fail({}, "LLVM contract adapter has no default self vtable");
                    return;
                }
                llvm::Value *value = llvm::Constant::getNullValue(typeOf(self->contract));
                value = builder.CreateInsertValue(value, receiver, 0);
                value = builder.CreateInsertValue(value, self->vtable, 1);
                receiver = value;
            } else {
                auto current = support.concrete;
                for (const auto field : support.targets[method].delegatePath) {
                    if (current.kind != TypeKind::Struct ||
                        current.declaration >= program_.structs.size() ||
                        field >= program_.structs[current.declaration].fields.size()) {
                        fail({}, "LLVM contract adapter has an invalid delegation path");
                        return;
                    }
                    receiver = builder.CreateStructGEP(
                        structTypes_[current.declaration], receiver,
                        structFieldIndex(current.declaration, field), "delegate.field");
                    current = program_.structs[current.declaration].fields[field].type;
                }
            }
            std::vector<llvm::Value *> arguments{receiver};
            for (std::size_t index = 1; index < adapter->arg_size(); ++index) {
                arguments.push_back(adapter->getArg(llvmIndex(index)));
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
        if (type.kind == TypeKind::View || type.kind == TypeKind::Edit ||
            type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst ||
            type.kind == TypeKind::Function) {
            return false;
        }
        return std::any_of(type.arguments.begin(), type.arguments.end(),
                           [&](const Type &argument) { return containsAggregate(argument); });
    }

    void declareChannelDrops() {
        for (FirFunctionId owner = 0; owner < program_.functions.size(); ++owner) {
            const auto &function = program_.functions[owner];
            for (const auto &expression : function.expressions) {
                const auto *channel = std::get_if<FirChannelExpression>(&expression.value);
                if (channel == nullptr || channel->payload == voidType ||
                    !typeRequiresDrop(channel->payload)) {
                    continue;
                }
                if (std::any_of(
                        channelDrops_.begin(), channelDrops_.end(),
                        [&](const ChannelDrop &drop) { return drop.type == channel->payload; })) {
                    continue;
                }
                if (auto *payload = typeOf(channel->payload);
                    payload == nullptr || payload->isVoidTy()) {
                    fail(expression.span, "LLVM backend cannot lower this channel payload drop");
                    continue;
                }
                const auto id = channelDrops_.size();
                auto *callback =
                    llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                                                   {pointerType()}, false),
                                           llvm::GlobalValue::InternalLinkage,
                                           "fdn_channel_drop_value_" + std::to_string(id), module_);
                channelDrops_.push_back({channel->payload, owner, callback});
            }
        }
    }

    llvm::Function *declareCallbackTarget(FirFunctionId id) {
        const auto &function = program_.functions[id];
        if (function.hasBody || !function.cSymbol.has_value()) {
            fail(function.sourceSpan, "LLVM callback declaration has no native start symbol");
            return nullptr;
        }
        std::vector<llvm::Type *> parameters;
        parameters.reserve(function.parameters.size() + 1);
        for (const auto local : function.parameters) {
            auto *parameter =
                local < function.locals.size() ? typeOf(function.locals[local].type) : nullptr;
            if (parameter == nullptr || parameter->isVoidTy()) {
                fail(function.sourceSpan, "LLVM callback declaration has an unsupported parameter");
                return nullptr;
            }
            parameters.push_back(parameter);
        }
        parameters.push_back(pointerType());
        auto callee = module_.getOrInsertFunction(
            *function.cSymbol,
            llvm::FunctionType::get(llvm::Type::getVoidTy(context_), parameters, false));
        auto *target = llvm::dyn_cast<llvm::Function>(callee.getCallee());
        if (target == nullptr) {
            fail(function.sourceSpan, "LLVM callback start symbol has a conflicting type");
        }
        return target;
    }

    void declareFunctions() {
        functions_.resize(program_.functions.size());
        nativeFunctions_.resize(program_.functions.size());
        for (FirFunctionId id = 0; id < program_.functions.size(); ++id) {
            const auto &function = program_.functions[id];
            if (function.task) {
                continue;
            }
            if (function.callback) {
                functions_[id] = declareCallbackTarget(id);
                continue;
            }
            auto *signature = functionType(function);
            if (signature == nullptr) {
                continue;
            }
            const auto linkage = options_.libraryPackage.has_value()
                                     ? llvm::GlobalValue::InternalLinkage
                                     : llvm::GlobalValue::ExternalLinkage;
            auto *declaration = llvm::Function::Create(signature, linkage,
                                                       functionName(program_, id), module_);
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
                    if (usesExternalResultPointer(function)) {
                        nativeFunctions_[id]->addParamAttr(
                            0, llvm::Attribute::getWithStructRetType(
                                   context_, typeOf(function.returnType)));
                    }
                }
            }
        }
        if (!diagnostics_.hasErrors()) {
            declareTaskAdapters();
        }
    }

    void declareTaskAdapters() {
        taskAdapters_.resize(program_.functions.size());
        for (FirFunctionId id = 0; id < program_.functions.size(); ++id) {
            const auto &function = program_.functions[id];
            if (!function.task) {
                continue;
            }

            TaskAdapter adapter;
            std::vector<llvm::Type *> fields;
            const auto addField = [&](llvm::Type *type) {
                const auto field = fields.size();
                fields.push_back(type);
                return field;
            };
            adapter.argumentFields.reserve(function.parameters.size());
            for (const auto local : function.parameters) {
                auto *type =
                    local < function.locals.size() ? typeOf(function.locals[local].type) : nullptr;
                if (type == nullptr || type->isVoidTy()) {
                    fail(function.sourceSpan, "LLVM task has an unsupported parameter type");
                    break;
                }
                adapter.argumentFields.push_back(addField(type));
            }
            adapter.argumentsActiveField = addField(llvm::Type::getInt1Ty(context_));
            adapter.stateField = addField(llvm::Type::getInt32Ty(context_));
            if (function.returnType != voidType) {
                auto *type = typeOf(function.returnType);
                if (type == nullptr || type->isVoidTy()) {
                    fail(function.sourceSpan, "LLVM task has an unsupported result type");
                    continue;
                }
                adapter.resultField = addField(type);
                adapter.resultActiveField = addField(llvm::Type::getInt1Ty(context_));
            }

            adapter.localFields.reserve(function.locals.size());
            adapter.localActiveFields.resize(function.locals.size());
            for (FirLocalId local = 0; local < function.locals.size(); ++local) {
                auto *type = typeOf(function.locals[local].type);
                if (type == nullptr || type->isVoidTy()) {
                    fail(function.sourceSpan, "LLVM task has an unsupported local type");
                    break;
                }
                adapter.localFields.push_back(addField(type));
                if (typeRequiresDrop(function.locals[local].type)) {
                    adapter.localActiveFields[local] = addField(llvm::Type::getInt1Ty(context_));
                }
            }

            adapter.expressionStates.resize(function.expressions.size());
            adapter.blockingFields.resize(function.expressions.size());
            adapter.callbackFields.resize(function.expressions.size());
            adapter.blockingWorkers.resize(function.expressions.size());
            adapter.callbackStarts.resize(function.expressions.size());
            adapter.callbackCancels.resize(function.expressions.size());
            adapter.statementStates.resize(function.statements.size());
            std::size_t state = 1;
            for (std::size_t expression = 0; expression < function.expressions.size();
                 ++expression) {
                const auto &value = function.expressions[expression].value;
                if (std::holds_alternative<FirTaskWaitExpression>(value) ||
                    std::holds_alternative<FirBlockingCallExpression>(value) ||
                    std::holds_alternative<FirCallbackCallExpression>(value) ||
                    std::holds_alternative<FirChannelSendExpression>(value) ||
                    std::holds_alternative<FirChannelReceiveExpression>(value)) {
                    adapter.expressionStates[expression] = state++;
                }
                if (std::holds_alternative<FirBlockingCallExpression>(value)) {
                    adapter.blockingFields[expression] = addField(pointerType());
                }
                if (std::holds_alternative<FirCallbackCallExpression>(value)) {
                    adapter.callbackFields[expression] = addField(pointerType());
                }
            }
            for (std::size_t statement = 0; statement < function.statements.size(); ++statement) {
                if (std::holds_alternative<FirSelectStatement>(
                        function.statements[statement].value)) {
                    adapter.statementStates[statement] = state++;
                }
            }
            if (diagnostics_.hasErrors()) {
                continue;
            }

            adapter.frame =
                llvm::StructType::create(context_, fields, "fdn.task.frame." + std::to_string(id));
            adapter.poll = llvm::Function::Create(
                llvm::FunctionType::get(llvm::Type::getInt32Ty(context_),
                                        {pointerType(), llvm::Type::getInt1Ty(context_)}, false),
                llvm::GlobalValue::InternalLinkage, "fdn_task_poll_" + std::to_string(id), module_);
            adapter.moveResult = llvm::Function::Create(
                llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                        {pointerType(), pointerType()}, false),
                llvm::GlobalValue::InternalLinkage, "fdn_task_move_result_" + std::to_string(id),
                module_);
            adapter.dropFrame = llvm::Function::Create(
                llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {pointerType()}, false),
                llvm::GlobalValue::InternalLinkage, "fdn_task_drop_frame_" + std::to_string(id),
                module_);
            taskAdapters_[id] = std::move(adapter);
        }
    }

    llvm::Value *frameField(llvm::StructType *frame, llvm::Value *value, std::size_t field,
                            std::string_view name = {}) {
        return builder_.CreateStructGEP(
            frame, value, llvmIndex(field),
            name.empty() ? llvm::Twine{} : llvm::Twine(llvm::StringRef(name.data(), name.size())));
    }

    void bindTaskFrame(TaskAdapter &adapter, llvm::Value *frame) {
        taskFrame_ = frame;
        locals_.assign(function_->locals.size(), nullptr);
        localActive_.assign(function_->locals.size(), nullptr);
        for (FirLocalId local = 0; local < function_->locals.size(); ++local) {
            locals_[local] =
                frameField(adapter.frame, frame, adapter.localFields[local], "task.local");
            if (adapter.localActiveFields[local].has_value()) {
                localActive_[local] = frameField(
                    adapter.frame, frame, *adapter.localActiveFields[local], "task.local.active");
            }
        }
    }

    void declareTaskHelpers(FirFunctionId id, TaskAdapter &adapter) {
        const auto &function = program_.functions[id];
        for (std::size_t expression = 0; expression < function.expressions.size(); ++expression) {
            if (std::holds_alternative<FirBlockingCallExpression>(
                    function.expressions[expression].value)) {
                adapter.blockingWorkers[expression] = llvm::Function::Create(
                    llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {pointerType()},
                                            false),
                    llvm::GlobalValue::InternalLinkage,
                    "fdn_task_blocking_" + std::to_string(id) + "_" + std::to_string(expression),
                    module_);
            }
            const auto *callback =
                std::get_if<FirCallbackCallExpression>(&function.expressions[expression].value);
            if (callback == nullptr) {
                continue;
            }
            adapter.callbackStarts[expression] = llvm::Function::Create(
                llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                        {pointerType(), pointerType()}, false),
                llvm::GlobalValue::InternalLinkage,
                "fdn_task_callback_start_" + std::to_string(id) + "_" + std::to_string(expression),
                module_);
            if (callback->function < program_.functions.size() &&
                program_.functions[callback->function].callbackCancelSymbol.has_value()) {
                adapter.callbackCancels[expression] =
                    llvm::Function::Create(llvm::FunctionType::get(llvm::Type::getVoidTy(context_),
                                                                   {pointerType()}, false),
                                           llvm::GlobalValue::InternalLinkage,
                                           "fdn_task_callback_cancel_" + std::to_string(id) + "_" +
                                               std::to_string(expression),
                                           module_);
            }
        }
    }

    llvm::CallInst *emitFunctionInvocation(FirFunctionId id,
                                           const std::vector<llvm::Value *> &arguments,
                                           llvm::Value *resultStorage, SourceSpan span) {
        if (id >= functions_.size() || functions_[id] == nullptr) {
            fail(span, "LLVM task helper has an invalid function target");
            return nullptr;
        }
        const auto &target = program_.functions[id];
        if (usesExternalResultPointer(target)) {
            if (resultStorage == nullptr) {
                fail(span, "LLVM task helper is missing indirect result storage");
                return nullptr;
            }
            auto values = arguments;
            values.insert(values.begin(), resultStorage);
            auto *call = builder_.CreateCall(functions_[id], values);
            call->addParamAttr(
                0, llvm::Attribute::getWithStructRetType(context_, typeOf(target.returnType)));
            return call;
        }
        auto *call = builder_.CreateCall(functions_[id], arguments);
        if (resultStorage != nullptr && target.returnType != voidType) {
            builder_.CreateStore(call, resultStorage);
        }
        return call;
    }

    void emitBlockingWorker(FirFunctionId id, TaskAdapter &adapter, FirExpressionId expressionId) {
        const auto &source = program_.functions[id];
        const auto *blocking =
            std::get_if<FirBlockingCallExpression>(&source.expressions[expressionId].value);
        auto *worker = adapter.blockingWorkers[expressionId];
        if (blocking == nullptr || worker == nullptr ||
            blocking->function >= program_.functions.size() ||
            !program_.functions[blocking->function].blocking) {
            fail(source.expressions[expressionId].span,
                 "LLVM blocking task helper has an invalid target");
            return;
        }

        function_ = &source;
        functionId_ = id;
        llvmFunction_ = worker;
        taskPoll_ = true;
        auto *entry = llvm::BasicBlock::Create(context_, "entry", worker);
        builder_.SetInsertPoint(entry);
        bindTaskFrame(adapter, worker->getArg(0));
        frame_ = builder_.CreateAlloca(frameType_, nullptr, "frame");
        enterFrame();

        std::vector<llvm::Value *> arguments;
        arguments.reserve(blocking->argumentStorages.size());
        for (const auto storage : blocking->argumentStorages) {
            arguments.push_back(loadLocal(storage));
        }
        llvm::Value *resultStorage{};
        if (blocking->resultStorage.has_value()) {
            resultStorage = locals_[*blocking->resultStorage];
        }
        emitFunctionInvocation(blocking->function, arguments, resultStorage,
                               source.expressions[expressionId].span);
        if (blocking->resultStorage.has_value()) {
            activateLocal(*blocking->resultStorage);
        }
        leaveFrame();
        builder_.CreateRetVoid();
    }

    llvm::Function *callbackCancelTarget(const FirFunction &target) {
        if (!target.callbackCancelSymbol.has_value()) {
            return nullptr;
        }
        auto callee = module_.getOrInsertFunction(
            *target.callbackCancelSymbol,
            llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {pointerType()}, false));
        return llvm::dyn_cast<llvm::Function>(callee.getCallee());
    }

    void emitCallbackHelpers(FirFunctionId id, TaskAdapter &adapter, FirExpressionId expressionId) {
        const auto &source = program_.functions[id];
        const auto *callback =
            std::get_if<FirCallbackCallExpression>(&source.expressions[expressionId].value);
        if (callback == nullptr || callback->function >= program_.functions.size() ||
            !program_.functions[callback->function].callback ||
            functions_[callback->function] == nullptr) {
            fail(source.expressions[expressionId].span,
                 "LLVM callback task helper has an invalid target");
            return;
        }

        function_ = &source;
        functionId_ = id;
        taskPoll_ = true;
        auto *start = adapter.callbackStarts[expressionId];
        llvmFunction_ = start;
        auto *entry = llvm::BasicBlock::Create(context_, "entry", start);
        builder_.SetInsertPoint(entry);
        bindTaskFrame(adapter, start->getArg(0));
        frame_ = builder_.CreateAlloca(frameType_, nullptr, "frame");
        enterNativeFrame(*program_.functions[callback->function].cSymbol,
                         source.expressions[expressionId].span);
        std::vector<llvm::Value *> arguments;
        arguments.reserve(callback->argumentStorages.size() + 1);
        for (const auto storage : callback->argumentStorages) {
            arguments.push_back(loadLocal(storage));
        }
        arguments.push_back(start->getArg(1));
        builder_.CreateCall(functions_[callback->function], arguments);
        leaveFrame();
        builder_.CreateRetVoid();

        auto *cancel = adapter.callbackCancels[expressionId];
        if (cancel == nullptr) {
            return;
        }
        auto *target = callbackCancelTarget(program_.functions[callback->function]);
        if (target == nullptr || !adapter.callbackFields[expressionId].has_value()) {
            fail(source.expressions[expressionId].span,
                 "LLVM callback cancellation has an invalid native target");
            return;
        }
        llvmFunction_ = cancel;
        entry = llvm::BasicBlock::Create(context_, "entry", cancel);
        builder_.SetInsertPoint(entry);
        frame_ = builder_.CreateAlloca(frameType_, nullptr, "frame");
        auto *operation =
            builder_.CreateLoad(pointerType(), frameField(adapter.frame, cancel->getArg(0),
                                                          *adapter.callbackFields[expressionId]));
        emitPanicUnless(
            builder_.CreateICmpNE(operation, llvm::ConstantPointerNull::get(pointerType())),
            "callback cancellation has no operation");
        enterNativeFrame(*program_.functions[callback->function].callbackCancelSymbol,
                         source.expressions[expressionId].span);
        builder_.CreateCall(target, {operation});
        leaveFrame();
        builder_.CreateRetVoid();
    }

    void emitTaskHelpers(FirFunctionId id, TaskAdapter &adapter) {
        for (FirExpressionId expression = 0; expression < program_.functions[id].expressions.size();
             ++expression) {
            if (adapter.blockingWorkers[expression] != nullptr) {
                emitBlockingWorker(id, adapter, expression);
            }
            if (adapter.callbackStarts[expression] != nullptr) {
                emitCallbackHelpers(id, adapter, expression);
            }
        }
    }

    void emitTaskAdapters() {
        for (FirFunctionId id = 0; id < taskAdapters_.size(); ++id) {
            if (!taskAdapters_[id].has_value()) {
                continue;
            }
            auto &adapter = *taskAdapters_[id];
            declareTaskHelpers(id, adapter);
            emitTaskHelpers(id, adapter);
            emitTaskPoll(id, adapter);
            clearDebugLocation();
            emitTaskMoveResult(id, adapter);
            emitTaskDropFrame(id, adapter);
        }
        resetFunctionState();
    }

    llvm::Function *declareDropHelper(const Type &type) {
        const auto found =
            std::find_if(dropHelpers_.begin(), dropHelpers_.end(),
                         [&](const DropHelper &helper) { return helper.type == type; });
        if (found != dropHelpers_.end()) {
            return found->function;
        }
        const auto id = dropHelpers_.size();
        auto *function = llvm::Function::Create(
            llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {pointerType()}, false),
            llvm::GlobalValue::InternalLinkage, "fdn_drop_value_" + std::to_string(id), module_);
        dropHelpers_.push_back(DropHelper{.type = type, .owner = functionId_, .function = function});
        return function;
    }

    void emitDropHelpers() {
        for (std::size_t index = 0; index < dropHelpers_.size(); ++index) {
            const auto type = dropHelpers_[index].type;
            const auto owner = dropHelpers_[index].owner;
            auto *function = dropHelpers_[index].function;
            if (owner >= program_.functions.size() || function == nullptr ||
                !function->empty()) {
                continue;
            }
            function_ = &program_.functions[owner];
            functionId_ = owner;
            llvmFunction_ = function;
            taskPoll_ = false;
            taskFrame_ = nullptr;
            frame_ = nullptr;
            locals_.clear();
            localActive_.clear();
            auto *entry = llvm::BasicBlock::Create(context_, "entry", function);
            builder_.SetInsertPoint(entry);
            emitCompositeDropAddress(function->getArg(0), type);
            if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                builder_.CreateRetVoid();
            }
        }
        resetFunctionState();
    }

    void emitChannelDrops() {
        for (auto &drop : channelDrops_) {
            if (drop.owner >= program_.functions.size()) {
                continue;
            }
            function_ = &program_.functions[drop.owner];
            functionId_ = drop.owner;
            llvmFunction_ = drop.function;
            taskPoll_ = false;
            taskFrame_ = nullptr;
            frame_ = nullptr;
            locals_.clear();
            localActive_.clear();
            auto *entry = llvm::BasicBlock::Create(context_, "entry", drop.function);
            builder_.SetInsertPoint(entry);
            dropAddress(drop.function->getArg(0), drop.type);
            if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                builder_.CreateRetVoid();
            }
        }
        resetFunctionState();
    }

    void resetFunctionState() {
        clearDebugLocation();
        function_ = nullptr;
        llvmFunction_ = nullptr;
        frame_ = nullptr;
        taskFrame_ = nullptr;
        taskAdapter_ = nullptr;
        taskPreviousCancellation_ = nullptr;
        taskPoll_ = false;
        locals_.clear();
        localActive_.clear();
        taskStateBlocks_.clear();
    }

    std::size_t taskStateCount(const TaskAdapter &adapter) const {
        std::size_t count{};
        for (const auto state : adapter.expressionStates) {
            if (state.has_value()) {
                count = std::max(count, *state);
            }
        }
        for (const auto state : adapter.statementStates) {
            if (state.has_value()) {
                count = std::max(count, *state);
            }
        }
        return count;
    }

    void emitTaskPending() {
        builder_.CreateCall(runtimeFunction("fdn_task_cancellation_leave",
                                            llvm::Type::getVoidTy(context_),
                                            {llvm::Type::getInt1Ty(context_)}),
                            {taskPreviousCancellation_});
        leaveFrame();
        builder_.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0));
    }

    void emitTaskReady(llvm::Value *result = nullptr) {
        if (taskAdapter_ == nullptr || taskFrame_ == nullptr) {
            fail(function_->sourceSpan, "LLVM task completion has no frame");
            return;
        }
        if (function_->returnType != voidType) {
            if (result == nullptr || !taskAdapter_->resultField.has_value() ||
                !taskAdapter_->resultActiveField.has_value()) {
                fail(function_->sourceSpan, "LLVM task completed without a result");
                return;
            }
            builder_.CreateStore(result, frameField(taskAdapter_->frame, taskFrame_,
                                                    *taskAdapter_->resultField, "task.result"));
            builder_.CreateStore(llvm::ConstantInt::getTrue(context_),
                                 frameField(taskAdapter_->frame, taskFrame_,
                                            *taskAdapter_->resultActiveField,
                                            "task.result.active"));
        }
        builder_.CreateCall(runtimeFunction("fdn_task_cancellation_leave",
                                            llvm::Type::getVoidTy(context_),
                                            {llvm::Type::getInt1Ty(context_)}),
                            {taskPreviousCancellation_});
        leaveFrame();
        builder_.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1));
    }

    void emitTaskPoll(FirFunctionId id, TaskAdapter &adapter) {
        function_ = &program_.functions[id];
        functionId_ = id;
        llvmFunction_ = adapter.poll;
        taskPoll_ = true;
        taskAdapter_ = &adapter;
        taskFrame_ = adapter.poll->getArg(0);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", adapter.poll);
        builder_.SetInsertPoint(entry);
        bindTaskFrame(adapter, taskFrame_);
        attachFunctionDebugInfo(*function_);
        for (FirLocalId local = 0; local < function_->locals.size(); ++local) {
            declareLocalDebugInfo(local);
        }
        frame_ = builder_.CreateAlloca(frameType_, nullptr, "frame");
        taskPreviousCancellation_ = builder_.CreateCall(
            runtimeFunction("fdn_task_cancellation_enter", llvm::Type::getInt1Ty(context_),
                            {llvm::Type::getInt1Ty(context_)}),
            {adapter.poll->getArg(1)}, "task.cancellation.previous");
        enterFrame();

        const auto states = taskStateCount(adapter);
        taskStateBlocks_.assign(states + 1, nullptr);
        auto *initial = llvm::BasicBlock::Create(context_, "task.initial", adapter.poll);
        auto *body = llvm::BasicBlock::Create(context_, "task.body", adapter.poll);
        auto *invalid = llvm::BasicBlock::Create(context_, "task.invalid", adapter.poll);
        auto *state = builder_.CreateLoad(
            llvm::Type::getInt32Ty(context_),
            frameField(adapter.frame, taskFrame_, adapter.stateField, "task.state"));
        auto *dispatch = builder_.CreateSwitch(state, invalid, llvmIndex(states + 1));
        dispatch->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0), initial);
        for (std::size_t index = 1; index <= states; ++index) {
            taskStateBlocks_[index] = llvm::BasicBlock::Create(
                context_, "task.state." + std::to_string(index), adapter.poll);
            dispatch->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), index),
                              taskStateBlocks_[index]);
        }

        builder_.SetInsertPoint(invalid);
        builder_.CreateCall(
            runtimeFunction("fdn_panic_cstr", llvm::Type::getVoidTy(context_), {pointerType()}),
            {builder_.CreateGlobalString("invalid task state", "task.invalid.message")});
        builder_.CreateUnreachable();

        builder_.SetInsertPoint(initial);
        for (std::size_t index = 0; index < function_->parameters.size(); ++index) {
            const auto local = function_->parameters[index];
            auto *argument = frameField(adapter.frame, taskFrame_, adapter.argumentFields[index],
                                        "task.argument");
            builder_.CreateStore(moveFromAddress(argument, function_->locals[local].type),
                                 locals_[local]);
            activateLocal(local);
        }
        builder_.CreateStore(llvm::ConstantInt::getFalse(context_),
                             frameField(adapter.frame, taskFrame_, adapter.argumentsActiveField,
                                        "task.arguments.active"));
        builder_.CreateBr(body);

        builder_.SetInsertPoint(body);
        const auto exits = emitBlock(function_->body);
        if (!diagnostics_.hasErrors() && !exits &&
            builder_.GetInsertBlock()->getTerminator() == nullptr) {
            if (function_->returnType == voidType) {
                emitTaskReady();
            } else if (function_->diverges) {
                builder_.CreateUnreachable();
            } else {
                fail(function_->sourceSpan, "LLVM task reached the end without a result");
            }
        }
    }

    void emitPanicUnless(llvm::Value *condition, std::string_view message) {
        auto *ready = llvm::BasicBlock::Create(context_, "check.ready", llvmFunction_);
        auto *failed = llvm::BasicBlock::Create(context_, "check.failed", llvmFunction_);
        builder_.CreateCondBr(condition, ready, failed);
        builder_.SetInsertPoint(failed);
        builder_.CreateCall(
            runtimeFunction("fdn_panic_cstr", llvm::Type::getVoidTy(context_), {pointerType()}),
            {builder_.CreateGlobalString(message, "check.message")});
        builder_.CreateUnreachable();
        builder_.SetInsertPoint(ready);
    }

    void emitTaskMoveResult(FirFunctionId id, TaskAdapter &adapter) {
        function_ = &program_.functions[id];
        functionId_ = id;
        llvmFunction_ = adapter.moveResult;
        taskPoll_ = false;
        auto *entry = llvm::BasicBlock::Create(context_, "entry", adapter.moveResult);
        builder_.SetInsertPoint(entry);
        if (function_->returnType == voidType) {
            builder_.CreateRetVoid();
            return;
        }
        auto *frame = adapter.moveResult->getArg(0);
        auto *target = adapter.moveResult->getArg(1);
        auto *active =
            frameField(adapter.frame, frame, *adapter.resultActiveField, "task.result.active");
        emitPanicUnless(
            builder_.CreateAnd(
                builder_.CreateLoad(llvm::Type::getInt1Ty(context_), active),
                builder_.CreateICmpNE(target, llvm::ConstantPointerNull::get(pointerType()))),
            "task result is unavailable");
        auto *source = frameField(adapter.frame, frame, *adapter.resultField, "task.result");
        builder_.CreateStore(moveFromAddress(source, function_->returnType), target);
        builder_.CreateStore(llvm::ConstantInt::getFalse(context_), active);
        builder_.CreateRetVoid();
    }

    void emitTaskDropFrame(FirFunctionId id, TaskAdapter &adapter) {
        function_ = &program_.functions[id];
        functionId_ = id;
        llvmFunction_ = adapter.dropFrame;
        taskPoll_ = true;
        taskAdapter_ = &adapter;
        taskFrame_ = adapter.dropFrame->getArg(0);
        frame_ = nullptr;
        auto *entry = llvm::BasicBlock::Create(context_, "entry", adapter.dropFrame);
        builder_.SetInsertPoint(entry);
        bindTaskFrame(adapter, taskFrame_);

        auto *dropArguments = llvm::BasicBlock::Create(context_, "arguments.drop", llvmFunction_);
        auto *afterArguments = llvm::BasicBlock::Create(context_, "arguments.done", llvmFunction_);
        builder_.CreateCondBr(builder_.CreateLoad(llvm::Type::getInt1Ty(context_),
                                                  frameField(adapter.frame, taskFrame_,
                                                             adapter.argumentsActiveField)),
                              dropArguments, afterArguments);
        builder_.SetInsertPoint(dropArguments);
        for (std::size_t index = function_->parameters.size(); index-- > 0;) {
            const auto local = function_->parameters[index];
            dropAddress(frameField(adapter.frame, taskFrame_, adapter.argumentFields[index]),
                        function_->locals[local].type);
        }
        builder_.CreateStore(llvm::ConstantInt::getFalse(context_),
                             frameField(adapter.frame, taskFrame_, adapter.argumentsActiveField));
        builder_.CreateBr(afterArguments);
        builder_.SetInsertPoint(afterArguments);

        for (std::size_t expression = 0; expression < function_->expressions.size(); ++expression) {
            if (adapter.blockingFields[expression].has_value()) {
                auto *slot =
                    frameField(adapter.frame, taskFrame_, *adapter.blockingFields[expression]);
                emitPanicUnless(
                    builder_.CreateICmpEQ(builder_.CreateLoad(pointerType(), slot),
                                          llvm::ConstantPointerNull::get(pointerType())),
                    "task frame still has blocking work");
            }
            if (adapter.callbackFields[expression].has_value()) {
                auto *slot =
                    frameField(adapter.frame, taskFrame_, *adapter.callbackFields[expression]);
                emitPanicUnless(
                    builder_.CreateICmpEQ(builder_.CreateLoad(pointerType(), slot),
                                          llvm::ConstantPointerNull::get(pointerType())),
                    "task frame still has callback work");
            }
        }
        for (std::size_t local = function_->locals.size(); local-- > 0;) {
            dropLocal(local);
        }
        if (function_->returnType != voidType) {
            auto *active = frameField(adapter.frame, taskFrame_, *adapter.resultActiveField);
            auto *drop = llvm::BasicBlock::Create(context_, "result.drop", llvmFunction_);
            auto *done = llvm::BasicBlock::Create(context_, "result.done", llvmFunction_);
            builder_.CreateCondBr(builder_.CreateLoad(llvm::Type::getInt1Ty(context_), active),
                                  drop, done);
            builder_.SetInsertPoint(drop);
            dropAddress(frameField(adapter.frame, taskFrame_, *adapter.resultField),
                        function_->returnType);
            builder_.CreateStore(llvm::ConstantInt::getFalse(context_), active);
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
        }
        builder_.CreateCall(
            runtimeFunction("fdn_dealloc", llvm::Type::getVoidTy(context_), {pointerType()}),
            {taskFrame_});
        builder_.CreateRetVoid();
    }

    llvm::Type *abiTypeOf(const Type &type) { return typeOf(type); }

    llvm::FunctionType *nativeFunctionType(const FirFunction &function) {
        const auto indirectResult = usesExternalResultPointer(function);
        auto *result = function.diverges || indirectResult ? llvm::Type::getVoidTy(context_)
                                                           : abiTypeOf(function.returnType);
        if (result == nullptr) {
            fail(function.sourceSpan,
                 "LLVM backend does not support the C ABI return type of " + function.name);
            return nullptr;
        }
        std::vector<llvm::Type *> parameters;
        parameters.reserve(function.parameters.size() + (indirectResult ? 1 : 0));
        if (indirectResult) {
            parameters.push_back(pointerType());
        }
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

    void storeEnumTag(llvm::Value* address, const Type& type, FirVariantId variant,
                      SourceSpan span) {
        if (address == nullptr || type.kind != TypeKind::Enum ||
            type.declaration >= program_.enums.size() ||
            variant >= program_.enums[type.declaration].variants.size()) {
            fail(span, "LLVM backend received an invalid enum tag destination");
            return;
        }
        auto* tag =
            builder_.CreateStructGEP(enumTypes_[type.declaration], address, 0, "enum.tag.address");
        builder_.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), variant),
                             tag);
    }

    llvm::Value* newStorage(const Type& type, const llvm::Twine& name, SourceSpan span) {
        auto* layout = typeOf(type);
        if (layout == nullptr || layout->isVoidTy()) {
            fail(span, "LLVM backend cannot allocate specialized value storage");
            return nullptr;
        }
        auto* storage = createEntryAlloca(layout, name);
        builder_.CreateStore(llvm::Constant::getNullValue(layout), storage);
        return storage;
    }

    void returnStoredValue(llvm::Value* storage, const Type& type) {
        leaveFrame();
        builder_.CreateRet(moveFromAddress(storage, type));
    }

    void dropTransitionParameters(const FirStateTransitionFunction& transition,
                                  bool preserveDestination) {
        for (std::size_t index = 1; index < function_->parameters.size(); ++index) {
            const auto parameter = function_->parameters[index];
            if ((preserveDestination && transition.destinationParameter == parameter) ||
                (index < function_->readParameters.size() && function_->readParameters[index]) ||
                parameter >= function_->locals.size()) {
                continue;
            }
            const auto& type = function_->locals[parameter].type;
            if (type.kind != TypeKind::View && type.kind != TypeKind::Edit) {
                dropAddress(locals_[parameter], type);
            }
        }
    }

    void emitStateTransitionFunction() {
        if (!function_->stateTransition.has_value() || function_->parameters.empty() ||
            function_->returnType.kind != TypeKind::Enum ||
            function_->returnType.declaration >= program_.enums.size() ||
            program_.enums[function_->returnType.declaration].variants.size() < 2 ||
            !program_.enums[function_->returnType.declaration].variants[1].payload.has_value()) {
            fail(function_->sourceSpan, "LLVM backend received an invalid state transition");
            return;
        }
        const auto& transition = *function_->stateTransition;
        const auto receiverLocal = function_->parameters.front();
        if (receiverLocal >= function_->locals.size()) {
            fail(function_->sourceSpan, "LLVM state transition has an invalid receiver");
            return;
        }
        const auto& receiverType = function_->locals[receiverLocal].type;
        if (receiverType.kind != TypeKind::Edit || receiverType.arguments.size() != 1 ||
            receiverType.arguments.front().kind != TypeKind::Enum) {
            fail(function_->sourceSpan, "LLVM state transition receiver is not editable state");
            return;
        }
        const auto machineType = receiverType.arguments.front();
        auto* receiver = loadLocal(receiverLocal);
        auto* result =
            newStorage(function_->returnType, "transition.result", function_->sourceSpan);
        if (receiver == nullptr || result == nullptr) {
            return;
        }

        auto* sourceTag = enumTag(receiver, machineType, function_->sourceSpan);
        llvm::Value* allowed = llvm::ConstantInt::getFalse(context_);
        for (const auto variant : transition.sourceVariants) {
            allowed = builder_.CreateOr(
                allowed,
                builder_.CreateICmpEQ(
                    sourceTag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), variant)));
        }
        auto* accepted = llvm::BasicBlock::Create(context_, "transition.accepted", llvmFunction_);
        auto* rejected = llvm::BasicBlock::Create(context_, "transition.rejected", llvmFunction_);
        builder_.CreateCondBr(allowed, accepted, rejected);

        builder_.SetInsertPoint(accepted);
        dropAddress(receiver, machineType);
        storeEnumTag(receiver, machineType, transition.destinationVariant, function_->sourceSpan);
        if (transition.destinationParameter.has_value()) {
            const auto parameter = *transition.destinationParameter;
            if (parameter >= function_->locals.size() ||
                transition.destinationVariant >=
                    program_.enums[machineType.declaration].variants.size() ||
                !program_.enums[machineType.declaration]
                     .variants[transition.destinationVariant]
                     .payload.has_value()) {
                fail(function_->sourceSpan,
                     "LLVM state transition has an invalid destination payload");
                return;
            }
            const auto& payloadType = *program_.enums[machineType.declaration]
                                           .variants[transition.destinationVariant]
                                           .payload;
            auto* payload = enumPayloadAddress(receiver, machineType, transition.destinationVariant,
                                               function_->sourceSpan);
            builder_.CreateStore(moveFromAddress(locals_[parameter], payloadType), payload);
        }
        dropTransitionParameters(transition, true);
        storeEnumTag(result, function_->returnType, 0, function_->sourceSpan);
        returnStoredValue(result, function_->returnType);

        builder_.SetInsertPoint(rejected);
        dropTransitionParameters(transition, false);
        const auto errorType =
            *program_.enums[function_->returnType.declaration].variants[1].payload;
        auto* error = newStorage(errorType, "transition.error", function_->sourceSpan);
        if (error == nullptr || errorType.kind != TypeKind::Enum ||
            errorType.declaration >= program_.enums.size() ||
            program_.enums[errorType.declaration].variants.empty()) {
            fail(function_->sourceSpan, "LLVM state transition has an invalid error type");
            return;
        }
        storeEnumTag(error, errorType, 0, function_->sourceSpan);
        auto* payload = enumPayloadAddress(result, function_->returnType, 1, function_->sourceSpan);
        builder_.CreateStore(moveFromAddress(error, errorType), payload);
        storeEnumTag(result, function_->returnType, 1, function_->sourceSpan);
        returnStoredValue(result, function_->returnType);
    }

    void emitStateTimeoutFunction() {
        if (!function_->stateTimeout.has_value() || function_->parameters.size() != 1 ||
            function_->returnType.kind != TypeKind::Enum ||
            function_->returnType.declaration >= program_.enums.size() ||
            program_.enums[function_->returnType.declaration].variants.size() < 2 ||
            !program_.enums[function_->returnType.declaration].variants[1].payload.has_value()) {
            fail(function_->sourceSpan, "LLVM backend received an invalid state timeout");
            return;
        }
        const auto& timeout = *function_->stateTimeout;
        const auto stateLocal = function_->parameters.front();
        if (stateLocal >= function_->locals.size()) {
            fail(function_->sourceSpan, "LLVM state timeout has an invalid state parameter");
            return;
        }
        const auto& stateType = function_->locals[stateLocal].type;
        const auto borrowed = stateType.kind == TypeKind::View && stateType.arguments.size() == 1 &&
                              stateType.arguments.front().kind == TypeKind::Enum;
        if (stateType.kind != TypeKind::Enum && !borrowed) {
            fail(function_->sourceSpan, "LLVM state timeout parameter is not state");
            return;
        }
        const auto machineType = borrowed ? stateType.arguments.front() : stateType;
        auto* state = borrowed ? loadLocal(stateLocal) : locals_[stateLocal];
        auto* result = newStorage(function_->returnType, "timeout.result", function_->sourceSpan);
        if (state == nullptr || result == nullptr) {
            return;
        }
        auto* sourceTag = enumTag(state, machineType, function_->sourceSpan);
        llvm::Value* covered = llvm::ConstantInt::getFalse(context_);
        for (const auto variant : timeout.sourceVariants) {
            covered = builder_.CreateOr(
                covered,
                builder_.CreateICmpEQ(
                    sourceTag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), variant)));
        }
        auto* some = llvm::BasicBlock::Create(context_, "timeout.some", llvmFunction_);
        auto* none = llvm::BasicBlock::Create(context_, "timeout.none", llvmFunction_);
        builder_.CreateCondBr(covered, some, none);

        builder_.SetInsertPoint(some);
        auto* payload = enumPayloadAddress(result, function_->returnType, 1, function_->sourceSpan);
        builder_.CreateStore(
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), timeout.nanoseconds), payload);
        storeEnumTag(result, function_->returnType, 1, function_->sourceSpan);
        returnStoredValue(result, function_->returnType);

        builder_.SetInsertPoint(none);
        storeEnumTag(result, function_->returnType, 0, function_->sourceSpan);
        returnStoredValue(result, function_->returnType);
    }

    llvm::Value* workflowArgument(FirFunctionId target, llvm::Value* address, const Type& valueType,
                                  SourceSpan span) {
        if (target >= program_.functions.size() || address == nullptr ||
            program_.functions[target].parameters.size() != 1) {
            fail(span, "LLVM workflow target has an invalid argument");
            return nullptr;
        }
        const auto& targetFunction = program_.functions[target];
        const auto parameter = targetFunction.parameters.front();
        if (parameter >= targetFunction.locals.size()) {
            fail(span, "LLVM workflow target has an invalid parameter");
            return nullptr;
        }
        const auto& parameterType = targetFunction.locals[parameter].type;
        if (parameterType.kind == TypeKind::View || parameterType.kind == TypeKind::Edit) {
            if (valueType.kind == TypeKind::View || valueType.kind == TypeKind::Edit) {
                return builder_.CreateLoad(typeOf(valueType), address, "workflow.argument");
            }
            return address;
        }
        auto* layout = typeOf(parameterType);
        if (layout == nullptr || layout->isVoidTy()) {
            fail(span, "LLVM workflow target argument has no layout");
            return nullptr;
        }
        return builder_.CreateLoad(layout, address, "workflow.argument");
    }

    llvm::Value* emitWorkflowCall(const FirWorkflowStep& step, FirFunctionId target,
                                  llvm::Value* argument, const Type& argumentType,
                                  bool compensation) {
        if (target >= program_.functions.size() || target >= functions_.size() ||
            functions_[target] == nullptr) {
            fail(function_->sourceSpan, "LLVM workflow has an invalid callable");
            return nullptr;
        }
        const auto& targetFunction = program_.functions[target];
        if (targetFunction.returnType.kind != TypeKind::Enum ||
            targetFunction.returnType.declaration >= program_.enums.size() ||
            program_.enums[targetFunction.returnType.declaration].variants.size() < 2) {
            fail(function_->sourceSpan, "LLVM workflow callable does not return Result");
            return nullptr;
        }
        const auto attempts = compensation ? std::size_t{1} : step.attempts;
        if (attempts == 0 || attempts > static_cast<std::size_t>(UINT32_MAX)) {
            fail(function_->sourceSpan, "LLVM workflow retry count is out of range");
            return nullptr;
        }
        auto* result =
            newStorage(targetFunction.returnType, "workflow.call.result", function_->sourceSpan);
        if (result == nullptr) {
            return nullptr;
        }
        auto* initial = builder_.GetInsertBlock();
        auto* call = llvm::BasicBlock::Create(context_, "workflow.call", llvmFunction_);
        auto* retry = llvm::BasicBlock::Create(context_, "workflow.retry", llvmFunction_);
        auto* done = llvm::BasicBlock::Create(context_, "workflow.call.done", llvmFunction_);
        builder_.CreateBr(call);
        builder_.SetInsertPoint(call);
        auto* attempt = builder_.CreatePHI(llvm::Type::getInt32Ty(context_), 2, "workflow.attempt");
        attempt->addIncoming(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0), initial);
        auto* value = workflowArgument(target, argument, argumentType, function_->sourceSpan);
        if (value == nullptr) {
            return nullptr;
        }
        auto* invocation = builder_.CreateCall(functions_[target], {value});
        builder_.CreateStore(invocation, result);
        auto* tag = enumTag(result, targetFunction.returnType, function_->sourceSpan);
        auto* succeeded =
            builder_.CreateICmpEQ(tag, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0));
        auto* nextAttempt = builder_.CreateAdd(
            attempt, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1));
        auto* exhausted = builder_.CreateICmpUGE(
            nextAttempt, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), attempts));
        builder_.CreateCondBr(builder_.CreateOr(succeeded, exhausted), done, retry);

        builder_.SetInsertPoint(retry);
        dropAddress(result, targetFunction.returnType);
        builder_.CreateCall(runtimeFunction("fdn_retry_wait", llvm::Type::getVoidTy(context_),
                                            {llvm::Type::getInt32Ty(context_)}),
                            {attempt});
        auto* retryBlock = builder_.GetInsertBlock();
        builder_.CreateBr(call);
        attempt->addIncoming(nextAttempt, retryBlock);
        builder_.SetInsertPoint(done);
        return result;
    }

    void emitWorkflowResult(llvm::Value* stepResult, const Type& stepResultType,
                            FirVariantId variant) {
        if (function_->returnType.kind != TypeKind::Enum ||
            function_->returnType.declaration >= program_.enums.size() ||
            stepResultType.kind != TypeKind::Enum ||
            stepResultType.declaration >= program_.enums.size() || variant > 1 ||
            variant >= program_.enums[function_->returnType.declaration].variants.size() ||
            variant >= program_.enums[stepResultType.declaration].variants.size()) {
            fail(function_->sourceSpan, "LLVM workflow has an invalid result shape");
            return;
        }
        auto* result = newStorage(function_->returnType, "workflow.return", function_->sourceSpan);
        if (result == nullptr) {
            return;
        }
        const auto& source = program_.enums[stepResultType.declaration].variants[variant].payload;
        const auto& destination =
            program_.enums[function_->returnType.declaration].variants[variant].payload;
        if (source.has_value() != destination.has_value() ||
            (source.has_value() && *source != *destination)) {
            fail(function_->sourceSpan, "LLVM workflow result payloads do not match");
            return;
        }
        if (source.has_value()) {
            auto* sourcePayload =
                enumPayloadAddress(stepResult, stepResultType, variant, function_->sourceSpan);
            auto* destinationPayload =
                enumPayloadAddress(result, function_->returnType, variant, function_->sourceSpan);
            builder_.CreateStore(moveFromAddress(sourcePayload, *source), destinationPayload);
        }
        storeEnumTag(result, function_->returnType, variant, function_->sourceSpan);
        returnStoredValue(result, function_->returnType);
    }

    void emitPipelineFunction(const FirWorkflowFunction& workflow) {
        if (function_->parameters.size() != 1 || workflow.steps.empty()) {
            fail(function_->sourceSpan, "LLVM backend received an invalid pipeline");
            return;
        }
        const auto input = function_->parameters.front();
        if (input >= function_->locals.size()) {
            fail(function_->sourceSpan, "LLVM pipeline has an invalid input");
            return;
        }
        llvm::Value* current = locals_[input];
        Type currentType = function_->locals[input].type;
        bool currentOwned{};

        for (std::size_t index = 0; index < workflow.steps.size(); ++index) {
            const auto& step = workflow.steps[index];
            if (step.function >= program_.functions.size()) {
                fail(function_->sourceSpan, "LLVM pipeline has an invalid step");
                return;
            }
            const auto& target = program_.functions[step.function];
            auto* stepResult = emitWorkflowCall(step, step.function, current, currentType, false);
            if (stepResult == nullptr) {
                return;
            }
            if (currentOwned) {
                dropAddress(current, currentType);
            }
            auto* failed = llvm::BasicBlock::Create(context_, "pipeline.failed", llvmFunction_);
            auto* succeeded =
                llvm::BasicBlock::Create(context_, "pipeline.succeeded", llvmFunction_);
            auto* tag = enumTag(stepResult, target.returnType, function_->sourceSpan);
            builder_.CreateCondBr(
                builder_.CreateICmpEQ(tag,
                                      llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
                failed, succeeded);

            builder_.SetInsertPoint(failed);
            emitWorkflowResult(stepResult, target.returnType, 1);

            builder_.SetInsertPoint(succeeded);
            if (index + 1 == workflow.steps.size()) {
                emitWorkflowResult(stepResult, target.returnType, 0);
                return;
            }
            const auto& success = program_.enums[target.returnType.declaration].variants[0].payload;
            if (!success.has_value()) {
                fail(function_->sourceSpan, "LLVM non-final pipeline step has no success payload");
                return;
            }
            current = newStorage(*success, "pipeline.value", function_->sourceSpan);
            if (current == nullptr) {
                return;
            }
            auto* payload =
                enumPayloadAddress(stepResult, target.returnType, 0, function_->sourceSpan);
            builder_.CreateStore(moveFromAddress(payload, *success), current);
            currentType = *success;
            currentOwned = true;
        }
    }

    llvm::Value* sagaCompensationFailures(const FirWorkflowFunction& workflow, std::size_t failed,
                                          llvm::Value* argument, const Type& argumentType,
                                          llvm::Value* details, const Type& originalError) {
        if (workflow.failureDetailsType.kind != TypeKind::Struct ||
            workflow.failureDetailsType.declaration >= program_.structs.size()) {
            fail(function_->sourceSpan, "LLVM saga has an invalid failure details type");
            return nullptr;
        }
        const auto& declaration = program_.structs[workflow.failureDetailsType.declaration];
        std::size_t compensationCapacity{};
        for (std::size_t current = failed; current > 0; --current) {
            compensationCapacity += workflow.steps[current - 1].compensation.has_value() ? 1 : 0;
        }
        if (declaration.fields.size() < 3 || declaration.fields[0].type != originalError ||
            declaration.fields[1].type != usizeType ||
            declaration.fields[2].type.kind != TypeKind::Array ||
            declaration.fields[2].type.arguments.size() != 1 ||
            declaration.fields[2].type.arguments.front() != originalError ||
            declaration.fields[2].type.declaration < compensationCapacity) {
            fail(function_->sourceSpan, "LLVM saga has an invalid compensation error storage");
            return nullptr;
        }
        auto* count = createEntryAlloca(sizeType(), "saga.compensation.count");
        builder_.CreateStore(llvm::ConstantInt::get(sizeType(), 0), count);
        for (std::size_t current = failed; current > 0; --current) {
            const auto& completed = workflow.steps[current - 1];
            if (!completed.compensation.has_value()) {
                continue;
            }
            const auto compensation = *completed.compensation;
            auto* compensationResult =
                emitWorkflowCall(completed, compensation, argument, argumentType, true);
            if (compensationResult == nullptr || compensation >= program_.functions.size()) {
                return nullptr;
            }
            const auto& compensationFunction = program_.functions[compensation];
            if (compensationFunction.returnType.kind != TypeKind::Enum ||
                compensationFunction.returnType.declaration >= program_.enums.size() ||
                program_.enums[compensationFunction.returnType.declaration].variants.size() < 2 ||
                !program_.enums[compensationFunction.returnType.declaration]
                     .variants[1]
                     .payload.has_value() ||
                *program_.enums[compensationFunction.returnType.declaration].variants[1].payload !=
                    originalError) {
                fail(function_->sourceSpan,
                     "LLVM saga compensation has an incompatible error type");
                return nullptr;
            }
            auto* failedCompensation =
                llvm::BasicBlock::Create(context_, "saga.compensation.failed", llvmFunction_);
            auto* compensationDone =
                llvm::BasicBlock::Create(context_, "saga.compensation.done", llvmFunction_);
            auto* tag =
                enumTag(compensationResult, compensationFunction.returnType, function_->sourceSpan);
            builder_.CreateCondBr(
                builder_.CreateICmpEQ(tag,
                                      llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
                failedCompensation, compensationDone);

            builder_.SetInsertPoint(failedCompensation);
            auto* errors =
                structFieldAddress(details, workflow.failureDetailsType, 2, function_->sourceSpan);
            auto* array =
                llvm::dyn_cast_or_null<llvm::ArrayType>(typeOf(declaration.fields[2].type));
            if (errors == nullptr || array == nullptr) {
                fail(function_->sourceSpan, "LLVM saga cannot address compensation error storage");
                return nullptr;
            }
            auto* index = builder_.CreateLoad(sizeType(), count, "saga.compensation.index");
            auto* destination = builder_.CreateInBoundsGEP(
                array, errors,
                {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0), index});
            auto* source = enumPayloadAddress(compensationResult, compensationFunction.returnType,
                                              1, function_->sourceSpan);
            builder_.CreateStore(moveFromAddress(source, originalError), destination);
            builder_.CreateStore(builder_.CreateAdd(index, llvm::ConstantInt::get(sizeType(), 1)),
                                 count);
            builder_.CreateBr(compensationDone);

            builder_.SetInsertPoint(compensationDone);
            dropAddress(compensationResult, compensationFunction.returnType);
        }
        return count;
    }

    void emitSagaFailure(const FirWorkflowFunction& workflow, std::size_t failed,
                         llvm::Value* failedResult, llvm::Value* argument,
                         const Type& argumentType) {
        if (failed >= workflow.steps.size() ||
            workflow.steps[failed].function >= program_.functions.size() ||
            workflow.failureType.kind != TypeKind::Enum ||
            workflow.failureType.declaration >= program_.enums.size() ||
            program_.enums[workflow.failureType.declaration].variants.size() < 2 ||
            function_->returnType.kind != TypeKind::Enum ||
            function_->returnType.declaration >= program_.enums.size() ||
            program_.enums[function_->returnType.declaration].variants.size() < 2 ||
            !program_.enums[function_->returnType.declaration].variants[1].payload.has_value() ||
            *program_.enums[function_->returnType.declaration].variants[1].payload !=
                workflow.failureType) {
            fail(function_->sourceSpan, "LLVM saga has an invalid failure shape");
            return;
        }
        const auto& failedFunction = program_.functions[workflow.steps[failed].function];
        if (failedFunction.returnType.kind != TypeKind::Enum ||
            failedFunction.returnType.declaration >= program_.enums.size() ||
            program_.enums[failedFunction.returnType.declaration].variants.size() < 2 ||
            !program_.enums[failedFunction.returnType.declaration]
                 .variants[1]
                 .payload.has_value()) {
            fail(function_->sourceSpan, "LLVM saga step has an invalid failure payload");
            return;
        }
        const auto originalType =
            *program_.enums[failedFunction.returnType.declaration].variants[1].payload;
        const auto& stepFailurePayload =
            program_.enums[workflow.failureType.declaration].variants[0].payload;
        const auto& compensationFailurePayload =
            program_.enums[workflow.failureType.declaration].variants[1].payload;
        if (!stepFailurePayload.has_value() || *stepFailurePayload != originalType ||
            !compensationFailurePayload.has_value() ||
            *compensationFailurePayload != workflow.failureDetailsType) {
            fail(function_->sourceSpan, "LLVM saga failure variants do not match workflow types");
            return;
        }
        auto* original = newStorage(originalType, "saga.original.error", function_->sourceSpan);
        auto* details =
            newStorage(workflow.failureDetailsType, "saga.failure.details", function_->sourceSpan);
        auto* failure = newStorage(workflow.failureType, "saga.failure", function_->sourceSpan);
        if (original == nullptr || details == nullptr || failure == nullptr) {
            return;
        }
        auto* failedPayload =
            enumPayloadAddress(failedResult, failedFunction.returnType, 1, function_->sourceSpan);
        builder_.CreateStore(moveFromAddress(failedPayload, originalType), original);
        auto* count = sagaCompensationFailures(workflow, failed, argument, argumentType, details,
                                               originalType);
        if (count == nullptr) {
            return;
        }
        auto* stepFailure = llvm::BasicBlock::Create(context_, "saga.step.failure", llvmFunction_);
        auto* compensationFailure =
            llvm::BasicBlock::Create(context_, "saga.compensation.failure", llvmFunction_);
        auto* failureReady =
            llvm::BasicBlock::Create(context_, "saga.failure.ready", llvmFunction_);
        auto* failureCount = builder_.CreateLoad(sizeType(), count, "saga.failure.count");
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(failureCount, llvm::ConstantInt::get(sizeType(), 0)), stepFailure,
            compensationFailure);

        builder_.SetInsertPoint(stepFailure);
        auto* stepPayload =
            enumPayloadAddress(failure, workflow.failureType, 0, function_->sourceSpan);
        builder_.CreateStore(moveFromAddress(original, originalType), stepPayload);
        storeEnumTag(failure, workflow.failureType, 0, function_->sourceSpan);
        builder_.CreateBr(failureReady);

        builder_.SetInsertPoint(compensationFailure);
        auto* originalField =
            structFieldAddress(details, workflow.failureDetailsType, 0, function_->sourceSpan);
        auto* countField =
            structFieldAddress(details, workflow.failureDetailsType, 1, function_->sourceSpan);
        builder_.CreateStore(moveFromAddress(original, originalType), originalField);
        builder_.CreateStore(failureCount, countField);
        auto* compensationPayload =
            enumPayloadAddress(failure, workflow.failureType, 1, function_->sourceSpan);
        builder_.CreateStore(moveFromAddress(details, workflow.failureDetailsType),
                             compensationPayload);
        storeEnumTag(failure, workflow.failureType, 1, function_->sourceSpan);
        builder_.CreateBr(failureReady);

        builder_.SetInsertPoint(failureReady);
        auto* result = newStorage(function_->returnType, "saga.return", function_->sourceSpan);
        if (result == nullptr) {
            return;
        }
        auto* payload = enumPayloadAddress(result, function_->returnType, 1, function_->sourceSpan);
        builder_.CreateStore(moveFromAddress(failure, workflow.failureType), payload);
        storeEnumTag(result, function_->returnType, 1, function_->sourceSpan);
        returnStoredValue(result, function_->returnType);
    }

    void emitSagaFunction(const FirWorkflowFunction& workflow) {
        if (function_->parameters.size() != 1 || workflow.steps.empty()) {
            fail(function_->sourceSpan, "LLVM backend received an invalid saga");
            return;
        }
        const auto input = function_->parameters.front();
        if (input >= function_->locals.size()) {
            fail(function_->sourceSpan, "LLVM saga has an invalid input");
            return;
        }
        auto* argument = locals_[input];
        const auto argumentType = function_->locals[input].type;
        for (std::size_t index = 0; index < workflow.steps.size(); ++index) {
            const auto& step = workflow.steps[index];
            if (step.function >= program_.functions.size()) {
                fail(function_->sourceSpan, "LLVM saga has an invalid step");
                return;
            }
            const auto& target = program_.functions[step.function];
            auto* stepResult = emitWorkflowCall(step, step.function, argument, argumentType, false);
            if (stepResult == nullptr) {
                return;
            }
            auto* failed = llvm::BasicBlock::Create(context_, "saga.failed", llvmFunction_);
            auto* succeeded = llvm::BasicBlock::Create(context_, "saga.succeeded", llvmFunction_);
            auto* tag = enumTag(stepResult, target.returnType, function_->sourceSpan);
            builder_.CreateCondBr(
                builder_.CreateICmpEQ(tag,
                                      llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
                failed, succeeded);

            builder_.SetInsertPoint(failed);
            emitSagaFailure(workflow, index, stepResult, argument, argumentType);

            builder_.SetInsertPoint(succeeded);
            if (index + 1 == workflow.steps.size()) {
                emitWorkflowResult(stepResult, target.returnType, 0);
                return;
            }
            dropAddress(stepResult, target.returnType);
        }
    }

    void emitWorkflowFunction() {
        if (!function_->workflow.has_value()) {
            fail(function_->sourceSpan, "LLVM backend received missing workflow metadata");
            return;
        }
        if (function_->workflow->kind == FirWorkflowKind::Pipeline) {
            emitPipelineFunction(*function_->workflow);
        } else {
            emitSagaFunction(*function_->workflow);
        }
    }

    void emitFunction(FirFunctionId id) {
        function_ = &program_.functions[id];
        functionId_ = id;
        llvmFunction_ = functions_[id];
        if (llvmFunction_ == nullptr || !llvmFunction_->empty()) {
            return;
        }
        taskPoll_ = false;
        taskAdapter_ = nullptr;
        taskFrame_ = nullptr;
        localActive_.assign(function_->locals.size(), nullptr);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", llvmFunction_);
        builder_.SetInsertPoint(entry);
        attachFunctionDebugInfo(*function_);
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
            declareLocalDebugInfo(local);
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
                auto *field = builder_.CreateStructGEP(environmentType, environment,
                                                       llvmIndex(captureIndex++));
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
        if (function_->stateTransition.has_value()) {
            emitStateTransitionFunction();
            clearDebugLocation();
            return;
        }
        if (function_->stateTimeout.has_value()) {
            emitStateTimeoutFunction();
            clearDebugLocation();
            return;
        }
        if (function_->workflow.has_value()) {
            emitWorkflowFunction();
            clearDebugLocation();
            return;
        }
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
        clearDebugLocation();
    }

    bool emitBlock(FirBlockId id) {
        if (id >= function_->blocks.size()) {
            fail(function_->sourceSpan, "LLVM backend received an invalid block");
            return true;
        }
        for (const auto statement : function_->blocks[id].statements) {
            if (taskPoll_ && emitSuspendingStatement(statement)) {
                if (builder_.GetInsertBlock()->getTerminator() != nullptr) {
                    return true;
                }
                continue;
            }
            if (emitStatement(statement)) {
                return true;
            }
        }
        dropLocals(function_->blocks[id].drops);
        return builder_.GetInsertBlock()->getTerminator() != nullptr;
    }

    bool beginSuspension(std::size_t state, SourceSpan span) {
        if (taskAdapter_ == nullptr || taskFrame_ == nullptr || state == 0 ||
            state >= taskStateBlocks_.size() || taskStateBlocks_[state] == nullptr) {
            fail(span, "LLVM task has an invalid suspension state");
            return false;
        }
        builder_.CreateStore(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), state),
            frameField(taskAdapter_->frame, taskFrame_, taskAdapter_->stateField, "task.state"));
        builder_.CreateBr(taskStateBlocks_[state]);
        builder_.SetInsertPoint(taskStateBlocks_[state]);
        setLocation(span);
        return true;
    }

    void finishSuspension(llvm::Value *ready, std::string_view name) {
        const auto prefix = std::string(name);
        auto *resume = llvm::BasicBlock::Create(context_, prefix + ".ready", llvmFunction_);
        auto *pending = llvm::BasicBlock::Create(context_, prefix + ".pending", llvmFunction_);
        builder_.CreateCondBr(ready, resume, pending);
        builder_.SetInsertPoint(pending);
        emitTaskPending();
        builder_.SetInsertPoint(resume);
    }

    void storeSuspendedResult(FirLocalId storage, std::optional<FirLocalId> target, bool discarded,
                              SourceSpan span) {
        if (storage >= function_->locals.size() || storage >= locals_.size()) {
            fail(span, "LLVM task operation has invalid result storage");
            return;
        }
        const auto &type = function_->locals[storage].type;
        if (target.has_value()) {
            if (*target >= function_->locals.size() || *target >= locals_.size()) {
                fail(span, "LLVM task operation has an invalid result binding");
                return;
            }
            builder_.CreateStore(moveFromAddress(locals_[storage], type), locals_[*target]);
            deactivateLocal(storage);
            activateLocal(*target);
            return;
        }
        if (discarded) {
            dropLocal(storage);
            return;
        }
        if (type != voidType) {
            fail(span, "LLVM task operation result is neither bound nor discarded");
        }
    }

    bool emitSuspendingWait(FirExpressionId expression, const FirTaskWaitExpression &wait,
                            std::optional<FirLocalId> resultLocal, bool discarded) {
        const auto span = function_->expressions[expression].span;
        const auto *task =
            wait.task < function_->expressions.size()
                ? std::get_if<FirMoveExpression>(&function_->expressions[wait.task].value)
                : nullptr;
        if (task == nullptr || task->local >= locals_.size()) {
            fail(span, "LLVM suspending task wait does not consume a local handle");
            return true;
        }
        const auto &resultType = function_->expressions[expression].type;
        if (!resultLocal.has_value() && resultType != voidType) {
            fail(span, "LLVM standalone task wait has a result");
            return true;
        }
        const auto state = taskAdapter_->expressionStates[expression];
        if (!state.has_value() || !beginSuspension(*state, span)) {
            return true;
        }
        llvm::Value *result = llvm::ConstantPointerNull::get(pointerType());
        if (resultLocal.has_value()) {
            result = locals_[*resultLocal];
        }
        auto *ready = builder_.CreateCall(runtimeFunction("fdn_task_poll_wait",
                                                          llvm::Type::getInt1Ty(context_),
                                                          {pointerType(), pointerType()}),
                                          {locals_[task->local], result}, "task.wait.ready");
        finishSuspension(ready, "task.wait");
        deactivateLocal(task->local);
        if (resultLocal.has_value()) {
            activateLocal(*resultLocal);
        }
        static_cast<void>(discarded);
        return true;
    }

    bool emitSuspendingBlocking(FirExpressionId expression,
                                const FirBlockingCallExpression &blocking,
                                std::optional<FirLocalId> resultLocal, bool discarded) {
        const auto span = function_->expressions[expression].span;
        if (blocking.arguments.size() != blocking.argumentStorages.size() ||
            !taskAdapter_->blockingFields[expression].has_value() ||
            taskAdapter_->blockingWorkers[expression] == nullptr) {
            fail(span, "LLVM blocking call has invalid persistent storage");
            return true;
        }
        for (std::size_t index = 0; index < blocking.arguments.size(); ++index) {
            const auto argument = emitExpression(blocking.arguments[index]);
            if (argument.diverges) {
                return true;
            }
            const auto parameter =
                blocking.argumentParameters.empty() ? index : blocking.argumentParameters[index];
            if (parameter >= blocking.argumentStorages.size()) {
                fail(span, "LLVM blocking call has an invalid argument mapping");
                return true;
            }
            const auto storage = blocking.argumentStorages[parameter];
            builder_.CreateStore(argument.value, locals_[storage]);
            activateLocal(storage);
        }
        const auto state = taskAdapter_->expressionStates[expression];
        if (!state.has_value() || !beginSuspension(*state, span)) {
            return true;
        }
        auto *slot = frameField(taskAdapter_->frame, taskFrame_,
                                *taskAdapter_->blockingFields[expression], "task.blocking.slot");
        auto *ready = builder_.CreateCall(
            runtimeFunction("fdn_blocking_poll", llvm::Type::getInt1Ty(context_),
                            {pointerType(), pointerType(), pointerType()}),
            {slot, taskFrame_, taskAdapter_->blockingWorkers[expression]}, "task.blocking.ready");
        finishSuspension(ready, "task.blocking");
        if (blocking.resultStorage.has_value()) {
            storeSuspendedResult(*blocking.resultStorage, resultLocal, discarded, span);
        }
        return true;
    }

    bool emitSuspendingCallback(FirExpressionId expression,
                                const FirCallbackCallExpression &callback,
                                std::optional<FirLocalId> resultLocal, bool discarded) {
        const auto span = function_->expressions[expression].span;
        if (callback.arguments.size() != callback.argumentStorages.size() ||
            !taskAdapter_->callbackFields[expression].has_value() ||
            taskAdapter_->callbackStarts[expression] == nullptr) {
            fail(span, "LLVM callback call has invalid persistent storage");
            return true;
        }
        for (std::size_t index = 0; index < callback.arguments.size(); ++index) {
            const auto argument = emitExpression(callback.arguments[index]);
            if (argument.diverges) {
                return true;
            }
            const auto parameter =
                callback.argumentParameters.empty() ? index : callback.argumentParameters[index];
            if (parameter >= callback.argumentStorages.size()) {
                fail(span, "LLVM callback call has an invalid argument mapping");
                return true;
            }
            const auto storage = callback.argumentStorages[parameter];
            builder_.CreateStore(argument.value, locals_[storage]);
            activateLocal(storage);
        }
        const auto state = taskAdapter_->expressionStates[expression];
        if (!state.has_value() || !beginSuspension(*state, span)) {
            return true;
        }
        auto *slot = frameField(taskAdapter_->frame, taskFrame_,
                                *taskAdapter_->callbackFields[expression], "task.callback.slot");
        llvm::Value *cancel = llvm::ConstantPointerNull::get(pointerType());
        if (taskAdapter_->callbackCancels[expression] != nullptr) {
            cancel = taskAdapter_->callbackCancels[expression];
        }
        auto *ready =
            builder_.CreateCall(runtimeFunction("fdn_reactor_poll", llvm::Type::getInt1Ty(context_),
                                                {pointerType(), pointerType(), pointerType(),
                                                 pointerType(), pointerType()}),
                                {slot, taskFrame_, taskAdapter_->callbackStarts[expression], cancel,
                                 locals_[callback.resultStorage]},
                                "task.callback.ready");
        finishSuspension(ready, "task.callback");
        if (resultLocal.has_value()) {
            builder_.CreateStore(loadLocal(callback.resultStorage), locals_[*resultLocal]);
            activateLocal(*resultLocal);
        } else if (!discarded && function_->expressions[expression].type != voidType) {
            fail(span, "LLVM callback result is neither bound nor discarded");
        }
        return true;
    }

    bool channelResultTypes(FirLocalId storage, Type &resultType, Type &errorType,
                            SourceSpan span) {
        if (storage >= function_->locals.size()) {
            fail(span, "LLVM channel operation has invalid Result storage");
            return false;
        }
        resultType = function_->locals[storage].type;
        if (resultType.kind != TypeKind::Enum || resultType.declaration >= program_.enums.size() ||
            program_.enums[resultType.declaration].variants.size() != 2 ||
            !program_.enums[resultType.declaration].variants[1].payload.has_value()) {
            fail(span, "LLVM channel operation has an invalid Result type");
            return false;
        }
        errorType = *program_.enums[resultType.declaration].variants[1].payload;
        if (errorType.kind != TypeKind::Enum || errorType.declaration >= program_.enums.size() ||
            program_.enums[errorType.declaration].variants.size() < 2) {
            fail(span, "LLVM channel operation has an invalid ChannelError type");
            return false;
        }
        return true;
    }

    void setEnumTag(llvm::Value *address, const Type &type, FirVariantId variant) {
        builder_.CreateStore(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), variant),
            builder_.CreateStructGEP(enumTypes_[type.declaration], address, 0, "enum.tag.address"));
    }

    bool emitSuspendingChannel(FirExpressionId expression, const FirChannelSendExpression *send,
                               const FirChannelReceiveExpression *receive,
                               std::optional<FirLocalId> resultLocal, bool discarded) {
        const auto span = function_->expressions[expression].span;
        const auto resultStorage = send != nullptr ? send->resultStorage : receive->resultStorage;
        Type resultType;
        Type errorType;
        if (!channelResultTypes(resultStorage, resultType, errorType, span)) {
            return true;
        }
        builder_.CreateStore(llvm::Constant::getNullValue(typeOf(resultType)),
                             locals_[resultStorage]);
        deactivateLocal(resultStorage);
        if (send != nullptr && send->value.has_value()) {
            if (!send->valueStorage.has_value()) {
                fail(span, "LLVM channel send has no persistent value storage");
                return true;
            }
            const auto value = emitExpression(*send->value);
            if (value.diverges) {
                return true;
            }
            builder_.CreateStore(value.value, locals_[*send->valueStorage]);
            activateLocal(*send->valueStorage);
        }
        const auto state = taskAdapter_->expressionStates[expression];
        if (!state.has_value() || !beginSuspension(*state, span)) {
            return true;
        }
        llvm::Value *status{};
        if (send != nullptr) {
            llvm::Value *value = llvm::ConstantPointerNull::get(pointerType());
            if (send->valueStorage.has_value()) {
                value = locals_[*send->valueStorage];
            }
            status = builder_.CreateCall(runtimeFunction("fdn_channel_poll_send",
                                                         llvm::Type::getInt32Ty(context_),
                                                         {pointerType(), pointerType()}),
                                         {loadLocal(send->sender), value}, "channel.send.status");
        } else {
            llvm::Value *value = llvm::ConstantPointerNull::get(pointerType());
            const auto &success = program_.enums[resultType.declaration].variants[0];
            if (success.payload.has_value()) {
                value = enumPayloadAddress(locals_[resultStorage], resultType, 0, span);
            }
            status = builder_.CreateCall(
                runtimeFunction("fdn_channel_poll_receive", llvm::Type::getInt32Ty(context_),
                                {pointerType(), pointerType()}),
                {loadLocal(receive->receiver), value}, "channel.receive.status");
        }
        finishSuspension(builder_.CreateICmpNE(
                             status, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0)),
                         send != nullptr ? "channel.send" : "channel.receive");

        if (send != nullptr && send->valueStorage.has_value()) {
            auto *transferred = llvm::BasicBlock::Create(context_, "channel.sent", llvmFunction_);
            auto *failed = llvm::BasicBlock::Create(context_, "channel.unsent", llvmFunction_);
            auto *done = llvm::BasicBlock::Create(context_, "channel.send.done", llvmFunction_);
            builder_.CreateCondBr(
                builder_.CreateICmpEQ(status,
                                      llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
                transferred, failed);
            builder_.SetInsertPoint(transferred);
            deactivateLocal(*send->valueStorage);
            builder_.CreateBr(done);
            builder_.SetInsertPoint(failed);
            dropLocal(*send->valueStorage);
            builder_.CreateBr(done);
            builder_.SetInsertPoint(done);
        }

        auto *success = llvm::BasicBlock::Create(context_, "channel.result.ok", llvmFunction_);
        auto *failure = llvm::BasicBlock::Create(context_, "channel.result.err", llvmFunction_);
        auto *complete = llvm::BasicBlock::Create(context_, "channel.result.done", llvmFunction_);
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(status,
                                  llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
            success, failure);
        builder_.SetInsertPoint(success);
        setEnumTag(locals_[resultStorage], resultType, 0);
        builder_.CreateBr(complete);
        builder_.SetInsertPoint(failure);
        auto *error = enumPayloadAddress(locals_[resultStorage], resultType, 1, span);
        auto *closed = builder_.CreateICmpEQ(
            status, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 2));
        builder_.CreateStore(
            builder_.CreateSelect(closed,
                                  llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0),
                                  llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
            builder_.CreateStructGEP(enumTypes_[errorType.declaration], error, 0));
        setEnumTag(locals_[resultStorage], resultType, 1);
        builder_.CreateBr(complete);
        builder_.SetInsertPoint(complete);
        activateLocal(resultStorage);
        storeSuspendedResult(resultStorage, resultLocal, discarded, span);
        return true;
    }

    void emitSelectSendCleanup(const FirSelectStatement &selection,
                               std::optional<std::size_t> selected) {
        for (std::size_t index = 0; index < selection.operations.size(); ++index) {
            const auto &arm = selection.operations[index];
            if (!arm.send || !arm.valueStorage.has_value()) {
                continue;
            }
            if (selected.has_value() && *selected == index) {
                deactivateLocal(*arm.valueStorage);
            } else {
                dropLocal(*arm.valueStorage);
            }
        }
    }

    llvm::Value *selectDeadline(const FirSelectStatement &selection, SourceSpan span) {
        auto *maximum = llvm::ConstantInt::getAllOnesValue(llvm::Type::getInt64Ty(context_));
        if (!selection.timeout.has_value()) {
            return maximum;
        }
        const auto &timeout = *selection.timeout;
        llvm::Value *duration{};
        if (timeout.duration.has_value()) {
            const auto value = emitExpression(*timeout.duration);
            if (value.diverges || value.value == nullptr ||
                !value.value->getType()->isIntegerTy()) {
                fail(span, "LLVM select timeout has an invalid duration");
                return nullptr;
            }
            duration = value.value;
            if (duration->getType() != llvm::Type::getInt64Ty(context_)) {
                duration =
                    builder_.CreateIntCast(duration, llvm::Type::getInt64Ty(context_), false);
            }
        } else {
            duration =
                llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), timeout.nanoseconds);
        }
        auto *now = builder_.CreateCall(
            runtimeFunction("fdn_monotonic_nanoseconds", llvm::Type::getInt64Ty(context_), {}), {},
            "select.now");
        auto *remaining = builder_.CreateSub(maximum, now);
        return builder_.CreateSelect(builder_.CreateICmpUGT(duration, remaining), maximum,
                                     builder_.CreateAdd(now, duration), "select.deadline");
    }

    void emitSelect(FirStatementId statementId, const FirSelectStatement &selection,
                    SourceSpan span) {
        if (selection.operations.empty() || statementId >= taskAdapter_->statementStates.size()) {
            fail(span, "LLVM select has no channel operations");
            return;
        }
        for (const auto &arm : selection.operations) {
            if (!arm.send) {
                if (arm.resultStorage < function_->locals.size()) {
                    const auto &resultType = function_->locals[arm.resultStorage].type;
                    if (auto *layout = typeOf(resultType);
                        layout != nullptr && !layout->isVoidTy()) {
                        builder_.CreateStore(llvm::Constant::getNullValue(layout),
                                             locals_[arm.resultStorage]);
                        deactivateLocal(arm.resultStorage);
                    }
                }
                continue;
            }
            if (!arm.value.has_value()) {
                continue;
            }
            if (!arm.valueStorage.has_value()) {
                fail(arm.span, "LLVM select send has no persistent value storage");
                return;
            }
            const auto value = emitExpression(*arm.value);
            if (value.diverges) {
                return;
            }
            builder_.CreateStore(value.value, locals_[*arm.valueStorage]);
            activateLocal(*arm.valueStorage);
        }

        auto *deadline = selectDeadline(selection, span);
        if (deadline == nullptr) {
            return;
        }
        builder_.CreateStore(deadline, locals_[selection.deadlineStorage]);
        const auto state = taskAdapter_->statementStates[statementId];
        if (!state.has_value() || !beginSuspension(*state, span)) {
            return;
        }

        auto *casesType = llvm::ArrayType::get(selectCaseType_, selection.operations.size());
        auto *cases = builder_.CreateAlloca(casesType, nullptr, "select.cases");
        for (std::size_t index = 0; index < selection.operations.size(); ++index) {
            const auto &arm = selection.operations[index];
            auto *item = builder_.CreateInBoundsGEP(
                casesType, cases,
                {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0),
                 llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), index)});
            builder_.CreateStore(loadLocal(arm.endpoint),
                                 builder_.CreateStructGEP(selectCaseType_, item, 0));
            llvm::Value *value = llvm::ConstantPointerNull::get(pointerType());
            if (arm.send && arm.valueStorage.has_value()) {
                value = locals_[*arm.valueStorage];
            } else if (!arm.send) {
                Type resultType;
                Type errorType;
                if (!channelResultTypes(arm.resultStorage, resultType, errorType, arm.span)) {
                    return;
                }
                static_cast<void>(errorType);
                if (program_.enums[resultType.declaration].variants[0].payload.has_value()) {
                    value = enumPayloadAddress(locals_[arm.resultStorage], resultType, 0, arm.span);
                }
            }
            builder_.CreateStore(value, builder_.CreateStructGEP(selectCaseType_, item, 1));
            builder_.CreateStore(
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), arm.send ? 1 : 2),
                builder_.CreateStructGEP(selectCaseType_, item, 2));
        }
        auto *selected = builder_.CreateAlloca(sizeType(), nullptr, "select.selected");
        builder_.CreateStore(llvm::ConstantInt::getAllOnesValue(sizeType()), selected);
        auto *status = builder_.CreateCall(
            runtimeFunction("fdn_channel_poll_select", llvm::Type::getInt32Ty(context_),
                            {pointerType(), pointerType(), sizeType(),
                             llvm::Type::getInt64Ty(context_), pointerType()}),
            {taskFrame_, cases, llvm::ConstantInt::get(sizeType(), selection.operations.size()),
             loadLocal(selection.deadlineStorage), selected},
            "select.status");
        finishSuspension(builder_.CreateICmpNE(
                             status, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0)),
                         "select");

        auto *timeout = llvm::BasicBlock::Create(context_, "select.timeout", llvmFunction_);
        auto *operation = llvm::BasicBlock::Create(context_, "select.operation", llvmFunction_);
        auto *error = llvm::BasicBlock::Create(context_, "select.error", llvmFunction_);
        auto *merge = llvm::BasicBlock::Create(context_, "select.end", llvmFunction_);
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(status,
                                  llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 4)),
            timeout, operation);

        bool continues{};
        builder_.SetInsertPoint(timeout);
        emitSelectSendCleanup(selection, std::nullopt);
        if (selection.timeout.has_value()) {
            if (!emitBlock(selection.timeout->body)) {
                builder_.CreateBr(merge);
                continues = true;
            }
        } else {
            builder_.CreateCall(
                runtimeFunction("fdn_panic_cstr", llvm::Type::getVoidTy(context_), {pointerType()}),
                {builder_.CreateGlobalString("select reached an unavailable timeout",
                                             "select.timeout.message")});
            builder_.CreateUnreachable();
        }

        builder_.SetInsertPoint(operation);
        auto *ready = llvm::BasicBlock::Create(context_, "select.ready", llvmFunction_);
        builder_.CreateCondBr(
            builder_.CreateICmpEQ(status,
                                  llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
            ready, error);

        builder_.SetInsertPoint(error);
        const auto &errorType = function_->locals[selection.errorLocal].type;
        if (errorType.kind != TypeKind::Enum || errorType.declaration >= program_.enums.size()) {
            fail(span, "LLVM select has an invalid ChannelError binding");
            builder_.CreateUnreachable();
        } else {
            auto *closed = builder_.CreateICmpEQ(
                status, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 2));
            builder_.CreateStore(
                builder_.CreateSelect(closed,
                                      llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0),
                                      llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
                builder_.CreateStructGEP(enumTypes_[errorType.declaration],
                                         locals_[selection.errorLocal], 0));
            activateLocal(selection.errorLocal);
            emitSelectSendCleanup(selection, std::nullopt);
            if (!emitBlock(selection.errorBlock)) {
                builder_.CreateBr(merge);
                continues = true;
            }
        }

        builder_.SetInsertPoint(ready);
        auto *invalid = llvm::BasicBlock::Create(context_, "select.invalid", llvmFunction_);
        auto *selectedValue = builder_.CreateLoad(sizeType(), selected, "select.index");
        auto *branches =
            builder_.CreateSwitch(selectedValue, invalid, llvmIndex(selection.operations.size()));
        for (std::size_t index = 0; index < selection.operations.size(); ++index) {
            const auto &arm = selection.operations[index];
            auto *branch = llvm::BasicBlock::Create(
                context_, "select.case." + std::to_string(index), llvmFunction_);
            branches->addCase(llvm::ConstantInt::get(sizeType(), index), branch);
            builder_.SetInsertPoint(branch);
            if (arm.binding.has_value()) {
                Type resultType;
                Type errorTypeValue;
                if (channelResultTypes(arm.resultStorage, resultType, errorTypeValue, arm.span)) {
                    static_cast<void>(errorTypeValue);
                    bindEnumPayload(locals_[arm.resultStorage], resultType, 0, *arm.binding, true,
                                    arm.span);
                }
            }
            emitSelectSendCleanup(selection, index);
            if (!emitBlock(arm.body)) {
                builder_.CreateBr(merge);
                continues = true;
            }
        }
        builder_.SetInsertPoint(invalid);
        builder_.CreateCall(
            runtimeFunction("fdn_panic_cstr", llvm::Type::getVoidTy(context_), {pointerType()}),
            {builder_.CreateGlobalString("invalid select branch", "select.invalid.message")});
        builder_.CreateUnreachable();

        builder_.SetInsertPoint(merge);
        if (!continues) {
            builder_.CreateUnreachable();
        }
    }

    bool emitSuspendingStatement(FirStatementId statementId) {
        if (statementId >= function_->statements.size() || taskAdapter_ == nullptr) {
            return false;
        }
        const auto &statement = function_->statements[statementId];
        if (const auto *selection = std::get_if<FirSelectStatement>(&statement.value)) {
            emitSelect(statementId, *selection, statement.span);
            return true;
        }

        std::optional<FirLocalId> resultLocal;
        bool discarded{};
        FirExpressionId expression{};
        if (const auto *variable = std::get_if<FirVariableStatement>(&statement.value)) {
            expression = variable->initializer;
            resultLocal = variable->local;
        } else if (const auto *expressionStatement =
                       std::get_if<FirExpressionStatement>(&statement.value)) {
            expression = expressionStatement->expression;
        } else if (const auto *discardStatement =
                       std::get_if<FirDiscardStatement>(&statement.value)) {
            expression = discardStatement->expression;
            discarded = true;
        } else {
            return false;
        }
        if (expression >= function_->expressions.size()) {
            return false;
        }
        const auto &value = function_->expressions[expression].value;
        if (const auto *wait = std::get_if<FirTaskWaitExpression>(&value)) {
            return emitSuspendingWait(expression, *wait, resultLocal, discarded);
        }
        if (const auto *blocking = std::get_if<FirBlockingCallExpression>(&value)) {
            return emitSuspendingBlocking(expression, *blocking, resultLocal, discarded);
        }
        if (const auto *callback = std::get_if<FirCallbackCallExpression>(&value)) {
            return emitSuspendingCallback(expression, *callback, resultLocal, discarded);
        }
        if (const auto *send = std::get_if<FirChannelSendExpression>(&value)) {
            return emitSuspendingChannel(expression, send, nullptr, resultLocal, discarded);
        }
        if (const auto *receive = std::get_if<FirChannelReceiveExpression>(&value)) {
            return emitSuspendingChannel(expression, nullptr, receive, resultLocal, discarded);
        }
        return false;
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
            if (value.diverges) {
                return true;
            }
            if (value.value == nullptr || variable->local >= locals_.size()) {
                if (!diagnostics_.hasErrors()) {
                    fail(statement.span, "LLVM variable initializer did not produce a value");
                }
                return true;
            }
            builder_.CreateStore(value.value, locals_[variable->local]);
            activateLocal(variable->local);
        } else if (const auto *letElse = std::get_if<FirLetElseStatement>(&statement.value)) {
            return emitLetElse(*letElse, statement.span);
        } else if (const auto *resultElse =
                       std::get_if<FirResultElseStatement>(&statement.value)) {
            return emitResultElse(*resultElse, statement.span);
        } else if (const auto *destructure =
                       std::get_if<FirStructDestructureStatement>(&statement.value)) {
            emitStructDestructure(*destructure, statement.span);
        } else if (const auto *assignment = std::get_if<FirAssignmentStatement>(&statement.value)) {
            if (assignment->operation != FirAssignmentOperator::Assign) {
                auto* address = emitAddress(assignment->target);
                if (address == nullptr) {
                    return true;
                }
                const auto type = function_->expressions[assignment->target].type;
                auto* left = builder_.CreateLoad(typeOf(type), address);
                const auto right = emitExpression(assignment->value);
                if (right.diverges || right.value == nullptr) {
                    return true;
                }
                auto* result = emitArithmetic(assignmentBinary(assignment->operation), type, left,
                                              right.value, statement.span);
                if (result == nullptr) {
                    return true;
                }
                if (type == stringType) {
                    dropAddress(address, type);
                }
                builder_.CreateStore(result, address);
                dropInspectedTemporary(assignment->value, right);
                return false;
            }
            const auto value = emitExpression(assignment->value);
            if (value.diverges) {
                return true;
            }
            if (value.value == nullptr) {
                if (!diagnostics_.hasErrors()) {
                    fail(statement.span, "LLVM assignment did not produce a value");
                }
                return true;
            }
            if (auto *address = emitAddress(assignment->target); address != nullptr) {
                const auto *local = std::get_if<FirLocalExpression>(
                    &function_->expressions[assignment->target].value);
                if (local != nullptr) {
                    dropLocal(local->local);
                } else {
                    dropAddress(address, function_->expressions[assignment->target].type);
                }
                builder_.CreateStore(value.value, address);
                if (local != nullptr) {
                    activateLocal(local->local);
                }
            }
        } else if (const auto *expression = std::get_if<FirExpressionStatement>(&statement.value)) {
            return emitExpression(expression->expression).diverges;
        } else if (const auto *discard = std::get_if<FirDiscardStatement>(&statement.value)) {
            const auto value = emitExpression(discard->expression);
            if (value.diverges) {
                return true;
            }
            dropValue(value.value, function_->expressions[discard->expression].type);
        } else if (const auto *returned = std::get_if<FirReturnStatement>(&statement.value)) {
            EmittedValue value;
            if (returned->value.has_value()) {
                value = emitExpression(*returned->value);
            }
            if (!value.diverges) {
                dropLocals(returned->drops);
                if (taskPoll_) {
                    emitTaskReady(returned->value.has_value() ? value.value : nullptr);
                } else {
                    leaveFrame();
                    if (returned->value.has_value()) {
                        builder_.CreateRet(value.value);
                    } else {
                        builder_.CreateRetVoid();
                    }
                }
            }
        } else if (const auto *branch = std::get_if<FirIfStatement>(&statement.value)) {
            return emitIf(*branch);
        } else if (const auto *whileLoop = std::get_if<FirWhileStatement>(&statement.value)) {
            emitWhile(*whileLoop);
        } else if (const auto *forLoop = std::get_if<FirForStatement>(&statement.value)) {
            emitFor(*forLoop, statement.span);
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
        return emitLetElseResult(binding, result, resultType, span);
    }

    bool emitLetElseResult(const FirLetElseStatement &binding, llvm::Value *result,
                           const Type &resultType, SourceSpan span) {
        if (result == nullptr) {
            fail(span, "LLVM Result binding has no value storage");
            return true;
        }
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
        return emitResultElseResult(binding, result, resultType, span);
    }

    bool emitResultElseResult(const FirResultElseStatement &binding, llvm::Value *result,
                              const Type &resultType, SourceSpan span) {
        if (result == nullptr) {
            fail(span, "LLVM Result handling has no value storage");
            return true;
        }
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
        const auto heapOwned = destructure.owned && sourceType.kind == TypeKind::Own;
        llvm::Value *source{};
        if (heapOwned) {
            source = initializer.value;
            if (sourceType.arguments.size() == 1) {
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
                auto *field = builder_.CreateStructGEP(
                    channelType_, source, llvmIndex(binding.field), "channel.field.address");
                builder_.CreateStore(moveFromAddress(field, function_->locals[binding.local].type),
                                     locals_[binding.local]);
                activateLocal(binding.local);
            }
            dropAddress(source, sourceType);
            if (heapOwned) {
                builder_.CreateCall(runtimeFunction("fdn_dealloc", llvm::Type::getVoidTy(context_),
                                                    {pointerType()}),
                                    {initializer.value});
            }
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
            activateLocal(binding.local);
        }
        if (heapOwned) {
            dropAddress(source, sourceType);
            builder_.CreateCall(
                runtimeFunction("fdn_dealloc", llvm::Type::getVoidTy(context_), {pointerType()}),
                {initializer.value});
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
        const auto sequence = emitExpression(loop.sequence);
        if (sequence.diverges || loop.sequenceStorage >= locals_.size() ||
            loop.index >= locals_.size() || loop.value >= locals_.size()) {
            return;
        }
        if (sequence.value == nullptr) {
            if (!diagnostics_.hasErrors()) {
                fail(span, "LLVM for sequence did not produce a value");
            }
            return;
        }
        builder_.CreateStore(sequence.value, locals_[loop.sequenceStorage]);
        activateLocal(loop.sequenceStorage);
        builder_.CreateStore(llvm::ConstantInt::get(sizeType(), 0), locals_[loop.index]);
        if (loop.next.has_value()) {
            const auto optionType = function_->expressions[*loop.next].type;
            if (optionType.kind != TypeKind::Enum ||
                optionType.declaration >= program_.enums.size() ||
                program_.enums[optionType.declaration].variants.size() != 2 ||
                !program_.enums[optionType.declaration].variants[1].payload.has_value()) {
                fail(span, "LLVM iterator has an invalid Option result");
                return;
            }
            auto *optionLayout = typeOf(optionType);
            if (optionLayout == nullptr || optionLayout->isVoidTy()) {
                fail(span, "LLVM iterator has no Option layout");
                return;
            }
            auto *nextStorage = createEntryAlloca(optionLayout, "iterator.next");
            auto *conditionBlock =
                llvm::BasicBlock::Create(context_, "iterator.condition", llvmFunction_);
            auto *noneBlock = llvm::BasicBlock::Create(context_, "iterator.none", llvmFunction_);
            auto *someBlock = llvm::BasicBlock::Create(context_, "iterator.some", llvmFunction_);
            auto *bodyBlock = llvm::BasicBlock::Create(context_, "iterator.body", llvmFunction_);
            auto *nextBlock = llvm::BasicBlock::Create(context_, "iterator.next", llvmFunction_);
            auto *invalidBlock =
                llvm::BasicBlock::Create(context_, "iterator.invalid", llvmFunction_);
            auto *exitBlock = llvm::BasicBlock::Create(context_, "iterator.end", llvmFunction_);
            builder_.CreateBr(conditionBlock);
            builder_.SetInsertPoint(conditionBlock);
            const auto next = emitExpression(*loop.next);
            if (next.diverges) {
                noneBlock->eraseFromParent();
                someBlock->eraseFromParent();
                bodyBlock->eraseFromParent();
                nextBlock->eraseFromParent();
                invalidBlock->eraseFromParent();
                exitBlock->eraseFromParent();
                return;
            }
            if (next.value == nullptr) {
                fail(span, "LLVM iterator did not produce an Option value");
                return;
            }
            builder_.CreateStore(next.value, nextStorage);
            auto *tag = enumTag(nextStorage, optionType, span);
            if (tag == nullptr) {
                return;
            }
            auto *dispatch = builder_.CreateSwitch(tag, invalidBlock, 2);
            dispatch->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0),
                              noneBlock);
            dispatch->addCase(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1),
                              someBlock);

            builder_.SetInsertPoint(noneBlock);
            dropAddress(nextStorage, optionType);
            builder_.CreateBr(exitBlock);
            builder_.SetInsertPoint(someBlock);
            if (!bindEnumPayload(nextStorage, optionType, 1, loop.value, true, span)) {
                return;
            }
            dropAddress(nextStorage, optionType);
            builder_.CreateBr(bodyBlock);
            builder_.SetInsertPoint(invalidBlock);
            builder_.CreateCall(
                runtimeFunction("fdn_invalid_enum_tag", llvm::Type::getVoidTy(context_), {}));
            builder_.CreateUnreachable();

            builder_.SetInsertPoint(bodyBlock);
            loops_.push_back({exitBlock, nextBlock});
            const auto exits = emitBlock(loop.body);
            loops_.pop_back();
            if (!exits) {
                builder_.CreateBr(nextBlock);
            }
            builder_.SetInsertPoint(nextBlock);
            auto *index = builder_.CreateCall(
                runtimeFunction("fdn_usize_add", sizeType(), {sizeType(), sizeType()}),
                {loadLocal(loop.index), llvm::ConstantInt::get(sizeType(), 1)});
            builder_.CreateStore(index, locals_[loop.index]);
            builder_.CreateBr(conditionBlock);
            builder_.SetInsertPoint(exitBlock);
            if (loop.ownsSequence) {
                dropLocal(loop.sequenceStorage);
            }
            for (auto cleanup = sequence.cleanups.rbegin();
                 cleanup != sequence.cleanups.rend(); ++cleanup) {
                dropAddress(cleanup->address, cleanup->type);
            }
            return;
        }
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
            dropLocal(loop.sequenceStorage);
        }
        for (auto cleanup = sequence.cleanups.rbegin(); cleanup != sequence.cleanups.rend();
             ++cleanup) {
            dropAddress(cleanup->address, cleanup->type);
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
            auto *value = moveFromAddress(address, expression.type);
            deactivateLocal(moved->local);
            return {value, false};
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
        if (const auto *spawn = std::get_if<FirSpawnExpression>(&expression.value)) {
            return emitSpawn(*spawn, expression.span);
        }
        if (const auto *wait = std::get_if<FirTaskWaitExpression>(&expression.value)) {
            if (taskPoll_) {
                fail(expression.span, "LLVM task wait did not lower as a suspending statement");
                return {};
            }
            return emitTaskWait(*wait, expression.type, expression.span);
        }
        if (const auto *channel = std::get_if<FirChannelExpression>(&expression.value)) {
            return emitChannel(*channel, expression.span);
        }
        if (const auto *clone = std::get_if<FirChannelSenderCloneExpression>(&expression.value)) {
            return emitChannelSenderClone(*clone, expression.span);
        }
        if (std::holds_alternative<FirBlockingCallExpression>(expression.value) ||
            std::holds_alternative<FirCallbackCallExpression>(expression.value) ||
            std::holds_alternative<FirChannelSendExpression>(expression.value) ||
            std::holds_alternative<FirChannelReceiveExpression>(expression.value)) {
            fail(expression.span, "LLVM suspending operation did not lower as a task statement");
            return {};
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
            return emitConditional(*conditional, expression.type, expression.span);
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

    EmittedValue emitFunctionValue(const FirFunctionValueExpression &function, const Type &type,
                                   SourceSpan span) {
        if (isCFunction(type)) {
            if (function.function >= nativeFunctions_.size() ||
                nativeFunctions_[function.function] == nullptr) {
                fail(span, "LLVM backend received a C function value without a C symbol");
                return {};
            }
            return {nativeFunctions_[function.function], false};
        }
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
                auto *field = builder_.CreateStructGEP(environmentType, environment,
                                                       llvmIndex(captureIndex++));
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
            auto operand = emitExpression(ownership.operand);
            if (operand.diverges) {
                return operand;
            }
            auto *value = operand.value;
            if (operandType.kind == TypeKind::Own && operandType.arguments.size() == 1 &&
                operandType.arguments.front().kind == TypeKind::Contract) {
                value = builder_.CreateLoad(typeOf(operandType.arguments.front()), operand.value,
                                            "owned.contract.view");
            }
            if (!isPlaceExpression(ownership.operand) && typeRequiresDrop(operandType)) {
                auto *address =
                    valueAddress(operand.value, operandType, "borrow.temporary");
                if (address == nullptr) {
                    fail(span, "LLVM backend cannot preserve this borrowed temporary");
                    return {};
                }
                operand.cleanups.push_back({address, operandType});
            }
            return {value, false, std::move(operand.cleanups)};
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
            std::vector<EmittedCleanup> cleanups;
            if (array.kind == TypeKind::Own && array.arguments.size() == 1) {
                auto operand = emitExpression(ownership.operand);
                if (operand.diverges) {
                    return operand;
                }
                address = operand.value;
                cleanups = std::move(operand.cleanups);
                if (!isPlaceExpression(ownership.operand)) {
                    auto *owner = valueAddress(address, array, "slice.owner.temporary");
                    if (owner == nullptr) {
                        fail(span, "LLVM backend cannot preserve this owned slice temporary");
                        return {};
                    }
                    cleanups.push_back({owner, array});
                }
                array = array.arguments.front();
            } else if (isPlaceExpression(ownership.operand)) {
                address = emitAddress(ownership.operand);
            } else {
                auto operand = emitExpression(ownership.operand);
                if (operand.diverges) {
                    return operand;
                }
                address = valueAddress(operand.value, array, "slice.temporary");
                cleanups = std::move(operand.cleanups);
                if (address != nullptr && typeRequiresDrop(array)) {
                    cleanups.push_back({address, array});
                }
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
            return {value, false, std::move(cleanups)};
        }
        if (operandType.kind == TypeKind::Own || operandType.kind == TypeKind::View ||
            operandType.kind == TypeKind::Edit) {
            auto operand = emitExpression(ownership.operand);
            if (operand.diverges) {
                return operand;
            }
            auto *value = operand.value;
            if (operandType.kind == TypeKind::Own && operandType.arguments.size() == 1 &&
                operandType.arguments.front().kind == TypeKind::Contract) {
                value = builder_.CreateLoad(typeOf(operandType.arguments.front()), operand.value,
                                            "owned.contract.view");
            }
            if (operandType.kind == TypeKind::Own &&
                !isPlaceExpression(ownership.operand)) {
                auto *owner = valueAddress(operand.value, operandType, "borrow.owner.temporary");
                if (owner == nullptr) {
                    fail(span, "LLVM backend cannot preserve this borrowed owner temporary");
                    return {};
                }
                operand.cleanups.push_back({owner, operandType});
            }
            return {value, false, std::move(operand.cleanups)};
        }
        if (isPlaceExpression(ownership.operand)) {
            if (auto *address = emitAddress(ownership.operand); address != nullptr) {
                return {address, false};
            }
            return {};
        }
        const auto operand = emitExpression(ownership.operand);
        if (operand.diverges) {
            return operand;
        }
        if (typeRequiresDrop(operandType)) {
            auto *address = valueAddress(operand.value, operandType, "borrow.temporary");
            if (address == nullptr) {
                fail(span, "LLVM backend cannot preserve this borrowed temporary");
                return {};
            }
            auto result = EmittedValue{address, false, std::move(operand.cleanups)};
            result.cleanups.push_back({address, operandType});
            return result;
        }
        return {valueAddress(operand.value, operandType, "borrow.temporary"), false,
                std::move(operand.cleanups)};
    }

    std::vector<llvm::Value *> orderValues(const std::vector<llvm::Value *> &values,
                                           const std::vector<std::size_t> &parameters,
                                           SourceSpan span) {
        if (parameters.empty()) {
            return values;
        }
        if (parameters.size() != values.size()) {
            fail(span, "LLVM call has an invalid argument mapping");
            return {};
        }
        std::vector<llvm::Value *> ordered(values.size());
        std::vector<bool> assigned(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto parameter = parameters[index];
            if (parameter >= values.size() || assigned[parameter]) {
                fail(span, "LLVM call has an invalid argument mapping");
                return {};
            }
            ordered[parameter] = values[index];
            assigned[parameter] = true;
        }
        return ordered;
    }

    EmittedValue emitSpawn(const FirSpawnExpression &spawn, SourceSpan span) {
        if (spawn.call >= function_->expressions.size()) {
            fail(span, "LLVM spawn has an invalid task call");
            return {};
        }
        const auto *call =
            std::get_if<FirCallExpression>(&function_->expressions[spawn.call].value);
        if (call == nullptr || call->kind != FirCallKind::Function ||
            call->function >= program_.functions.size() ||
            !program_.functions[call->function].task || call->function >= taskAdapters_.size() ||
            !taskAdapters_[call->function].has_value()) {
            fail(span, "LLVM spawn did not receive a task call");
            return {};
        }

        std::vector<llvm::Value *> arguments;
        arguments.reserve(call->arguments.size());
        for (const auto argument : call->arguments) {
            const auto value = emitExpression(argument);
            if (value.diverges) {
                return value;
            }
            arguments.push_back(value.value);
        }
        arguments = orderValues(arguments, call->argumentParameters, span);
        if (arguments.size() != call->arguments.size()) {
            return {};
        }

        auto &adapter = *taskAdapters_[call->function];
        const auto size = module_.getDataLayout().getTypeAllocSize(adapter.frame);
        if (size.isScalable()) {
            fail(span, "LLVM cannot allocate a scalable task frame");
            return {};
        }
        auto *frame = builder_.CreateCall(
            runtimeFunction("fdn_alloc", pointerType(), {sizeType()}),
            {llvm::ConstantInt::get(sizeType(), size.getFixedValue())}, "task.frame");
        builder_.CreateStore(llvm::Constant::getNullValue(adapter.frame), frame);
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            builder_.CreateStore(
                arguments[index],
                frameField(adapter.frame, frame, adapter.argumentFields[index], "task.argument"));
        }
        builder_.CreateStore(llvm::ConstantInt::getTrue(context_),
                             frameField(adapter.frame, frame, adapter.argumentsActiveField,
                                        "task.arguments.active"));
        auto *task = builder_.CreateCall(
            runtimeFunction("fdn_task_spawn", pointerType(),
                            {pointerType(), pointerType(), pointerType(), pointerType()}),
            {frame, adapter.poll, adapter.moveResult, adapter.dropFrame}, "task");
        return {task, false};
    }

    EmittedValue emitTaskWait(const FirTaskWaitExpression &wait, const Type &type,
                              SourceSpan span) {
        const auto task = emitExpression(wait.task);
        if (task.diverges) {
            return task;
        }
        if (task.value == nullptr || !task.value->getType()->isPointerTy()) {
            fail(span, "LLVM task wait has an invalid handle");
            return {};
        }
        auto *handle = builder_.CreateAlloca(pointerType(), nullptr, "task.wait.handle");
        builder_.CreateStore(task.value, handle);
        if (type == voidType) {
            builder_.CreateCall(runtimeFunction("fdn_task_wait", llvm::Type::getVoidTy(context_),
                                                {pointerType(), pointerType()}),
                                {handle, llvm::ConstantPointerNull::get(pointerType())});
            return {nullptr, false};
        }
        auto *layout = typeOf(type);
        if (layout == nullptr || layout->isVoidTy()) {
            fail(span, "LLVM task wait has an unsupported result type");
            return {};
        }
        auto *result = builder_.CreateAlloca(layout, nullptr, "task.wait.result");
        builder_.CreateStore(llvm::Constant::getNullValue(layout), result);
        builder_.CreateCall(runtimeFunction("fdn_task_wait", llvm::Type::getVoidTy(context_),
                                            {pointerType(), pointerType()}),
                            {handle, result});
        return {builder_.CreateLoad(layout, result, "task.wait.value"), false};
    }

    llvm::Function *channelDropFunction(const Type &type) const {
        const auto found = std::find_if(channelDrops_.begin(), channelDrops_.end(),
                                        [&](const ChannelDrop &drop) { return drop.type == type; });
        return found == channelDrops_.end() ? nullptr : found->function;
    }

    EmittedValue emitChannel(const FirChannelExpression &channel, SourceSpan span) {
        const auto capacity = emitExpression(channel.capacity);
        if (capacity.diverges) {
            return capacity;
        }
        auto *capacityValue = capacity.value;
        if (capacityValue == nullptr || !capacityValue->getType()->isIntegerTy()) {
            fail(span, "LLVM channel capacity is not an integer");
            return {};
        }
        if (capacityValue->getType() != sizeType()) {
            capacityValue = builder_.CreateIntCast(capacityValue, sizeType(), false);
        }

        std::uint64_t payloadSize{};
        if (channel.payload != voidType) {
            auto *payload = typeOf(channel.payload);
            if (payload == nullptr || payload->isVoidTy()) {
                fail(span, "LLVM channel has an unsupported payload type");
                return {};
            }
            const auto size = module_.getDataLayout().getTypeAllocSize(payload);
            if (size.isScalable()) {
                fail(span, "LLVM channel payload has a scalable layout");
                return {};
            }
            payloadSize = size.getFixedValue();
        }
        llvm::Value *drop = llvm::ConstantPointerNull::get(pointerType());
        if (typeRequiresDrop(channel.payload)) {
            drop = channelDropFunction(channel.payload);
            if (drop == nullptr) {
                fail(span, "LLVM channel payload drop callback is unavailable");
                return {};
            }
        }
        auto *sender = builder_.CreateAlloca(pointerType(), nullptr, "channel.sender");
        auto *receiver = builder_.CreateAlloca(pointerType(), nullptr, "channel.receiver");
        builder_.CreateStore(llvm::ConstantPointerNull::get(pointerType()), sender);
        builder_.CreateStore(llvm::ConstantPointerNull::get(pointerType()), receiver);
        builder_.CreateCall(
            runtimeFunction("fdn_channel_open", llvm::Type::getVoidTy(context_),
                            {sizeType(), sizeType(), pointerType(), pointerType(), pointerType()}),
            {llvm::ConstantInt::get(sizeType(), payloadSize), capacityValue, drop, sender,
             receiver});
        llvm::Value *result = llvm::Constant::getNullValue(channelType_);
        result = builder_.CreateInsertValue(result, builder_.CreateLoad(pointerType(), sender), 0);
        result =
            builder_.CreateInsertValue(result, builder_.CreateLoad(pointerType(), receiver), 1);
        return {result, false};
    }

    EmittedValue emitChannelSenderClone(const FirChannelSenderCloneExpression &clone,
                                        SourceSpan span) {
        const auto sender = emitExpression(clone.sender);
        if (sender.diverges) {
            return sender;
        }
        if (sender.value == nullptr || !sender.value->getType()->isPointerTy()) {
            fail(span, "LLVM sender clone has an invalid handle");
            return {};
        }
        return {builder_.CreateCall(
                    runtimeFunction("fdn_channel_clone_sender", pointerType(), {pointerType()}),
                    {sender.value}, "channel.sender.clone"),
                false};
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
            value = builder_.CreateInsertValue(value, payload.value,
                                               llvmIndex(constructor.variant + 1));
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
        if (taskPoll_ && match.valueStorage.has_value()) {
            const auto persistent = *match.valueStorage;
            if (persistent >= locals_.size() || inspected.value == nullptr) {
                fail(span, "LLVM task match has invalid persistent value storage");
                return {};
            }
            builder_.CreateStore(inspected.value, locals_[persistent]);
            activateLocal(persistent);
            if (inspectedType.kind == TypeKind::Own || inspectedType.kind == TypeKind::View ||
                inspectedType.kind == TypeKind::Edit) {
                storage = loadLocal(persistent);
            } else {
                storage = locals_[persistent];
            }
        } else if (inspectedType.kind == TypeKind::Own ||
                   inspectedType.kind == TypeKind::View ||
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
            result = createEntryAlloca(resultType, "match.result");
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
                    if (taskPoll_ && emitSuspendingStatement(statement)) {
                        if (builder_.GetInsertBlock()->getTerminator() != nullptr) {
                            exits = true;
                            break;
                        }
                        continue;
                    }
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
                    if (taskPoll_ && match.valueStorage.has_value()) {
                        dropLocal(*match.valueStorage);
                    } else {
                        dropMatchValue(storage, inspected.value, inspectedType);
                    }
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
        case FirUnaryOperator::BitwiseNot:
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

    llvm::Value *emitArithmetic(FirBinaryOperator operation, const Type &type,
                                llvm::Value *left, llvm::Value *right, SourceSpan span) {
        if ((type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) &&
            (operation == FirBinaryOperator::Add ||
             operation == FirBinaryOperator::Subtract) &&
            type.arguments.size() == 1) {
            auto *offset = integerToSize(right);
            if (operation == FirBinaryOperator::Subtract) {
                offset = builder_.CreateNeg(offset);
            }
            return builder_.CreateInBoundsGEP(typeOf(type.arguments.front()), left, offset);
        }
        if (type == stringType && operation == FirBinaryOperator::Add) {
            auto *result = builder_.CreateAlloca(stringType_, nullptr, "string.concat.result");
            builder_.CreateCall(
                runtimeFunction("fdn_abi_string_concat", llvm::Type::getVoidTy(context_),
                                {pointerType(), pointerType(), pointerType()}),
                {result, stringAddress(left), stringAddress(right)});
            return builder_.CreateLoad(stringType_, result);
        }
        if (isFloating(type)) {
            switch (operation) {
            case FirBinaryOperator::Add:
                return builder_.CreateFAdd(left, right);
            case FirBinaryOperator::Subtract:
                return builder_.CreateFSub(left, right);
            case FirBinaryOperator::Multiply:
                return builder_.CreateFMul(left, right);
            case FirBinaryOperator::Divide:
                return builder_.CreateFDiv(left, right);
            case FirBinaryOperator::Remainder:
                return builder_.CreateFRem(left, right);
            default:
                break;
            }
        }
        if (isInteger(type)) {
            if (operation == FirBinaryOperator::BitwiseAnd) {
                return builder_.CreateAnd(left, right);
            }
            if (operation == FirBinaryOperator::BitwiseXor) {
                return builder_.CreateXor(left, right);
            }
            if (operation == FirBinaryOperator::BitwiseOr) {
                return builder_.CreateOr(left, right);
            }
            std::string name;
            switch (operation) {
            case FirBinaryOperator::Add:
                name = "add";
                break;
            case FirBinaryOperator::Subtract:
                name = "subtract";
                break;
            case FirBinaryOperator::Multiply:
                name = "multiply";
                break;
            case FirBinaryOperator::Divide:
                name = "divide";
                break;
            case FirBinaryOperator::Remainder:
                name = "remainder";
                break;
            case FirBinaryOperator::ShiftLeft:
                name = "shift_left";
                break;
            case FirBinaryOperator::ShiftRight:
                name = "shift_right";
                break;
            default:
                break;
            }
            setLocation(span);
            auto callee = runtimeFunction("fdn_" + integerTypeTag(type) + "_" + name,
                                          typeOf(type), {typeOf(type), typeOf(type)});
            return builder_.CreateCall(callee, {left, right});
        }
        fail(span, "LLVM backend has not lowered this arithmetic operation yet");
        return nullptr;
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
        const auto finish = [&](llvm::Value *result) {
            dropInspectedTemporary(binary.left, left);
            dropInspectedTemporary(binary.right, right);
            return EmittedValue{result, false};
        };
        switch (binary.operation) {
        case FirBinaryOperator::Add:
        case FirBinaryOperator::Subtract:
        case FirBinaryOperator::Multiply:
        case FirBinaryOperator::Divide:
        case FirBinaryOperator::Remainder:
        case FirBinaryOperator::ShiftLeft:
        case FirBinaryOperator::ShiftRight:
        case FirBinaryOperator::BitwiseAnd:
        case FirBinaryOperator::BitwiseXor:
        case FirBinaryOperator::BitwiseOr:
            return finish(emitArithmetic(binary.operation, type, left.value, right.value, span));
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
                return finish(binary.operation == FirBinaryOperator::Equal
                                  ? equal
                                  : builder_.CreateNot(equal));
            }
            if (left.value->getType()->isFloatingPointTy()) {
                auto *comparison = binary.operation == FirBinaryOperator::Equal
                                       ? builder_.CreateFCmpOEQ(left.value, right.value)
                                       : builder_.CreateFCmpUNE(left.value, right.value);
                return finish(comparison);
            }
            return finish(binary.operation == FirBinaryOperator::Equal
                              ? builder_.CreateICmpEQ(left.value, right.value)
                              : builder_.CreateICmpNE(left.value, right.value));
        case FirBinaryOperator::Less:
        case FirBinaryOperator::LessEqual:
        case FirBinaryOperator::Greater:
        case FirBinaryOperator::GreaterEqual:
            return finish(emitComparison(binary.operation, left.value, right.value,
                                         function_->expressions[binary.left].type)
                              .value);
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
        if (call.kind == FirCallKind::CString) {
            if (call.arguments.size() != 1 || !call.typeArguments.empty()) {
                fail(span, "LLVM cString call has invalid arguments");
                return {};
            }
            const auto *literal = std::get_if<FirStringExpression>(
                &function_->expressions[call.arguments.front()].value);
            if (literal == nullptr) {
                fail(span, "LLVM cString argument is not a string literal");
                return {};
            }
            return {builder_.CreateGlobalString(literal->value, "cstring"), false};
        }
        std::vector<EmittedValue> arguments;
        arguments.reserve(call.arguments.size());
        for (const auto argument : call.arguments) {
            arguments.push_back(emitExpression(argument));
            if (arguments.back().diverges) {
                return arguments.back();
            }
        }
        std::vector<llvm::Value *> sourceValues;
        sourceValues.reserve(arguments.size());
        std::transform(arguments.begin(), arguments.end(), std::back_inserter(sourceValues),
                       [](const auto &argument) { return argument.value; });
        auto values = orderValues(sourceValues, call.argumentParameters, span);
        if (values.size() != sourceValues.size()) {
            return {};
        }
        if (call.kind == FirCallKind::Contract && !contractCallConsumesReceiver(call) &&
            !call.arguments.empty() && !isPlaceExpression(call.arguments.front())) {
            const auto &receiverType = function_->expressions[call.arguments.front()].type;
            if (receiverType.kind == TypeKind::Own) {
                auto *owner = valueAddress(sourceValues.front(), receiverType,
                                           "contract.receiver.owner.temporary");
                if (owner == nullptr) {
                    fail(span, "LLVM backend cannot preserve this contract owner temporary");
                    return {};
                }
                arguments.front().cleanups.push_back({owner, receiverType});
            }
        }
        setLocation(span);
        llvm::Value *result{};
        switch (call.kind) {
        case FirCallKind::Constrained:
            fail(span, "LLVM backend received an unresolved constrained call");
            return {};
        case FirCallKind::Function:
            if (call.function >= functions_.size() || functions_[call.function] == nullptr) {
                fail(span, "LLVM backend received an invalid function call");
                return {};
            }
            if (program_.functions[call.function].diverges) {
                builder_.CreateCall(functions_[call.function], values);
                builder_.CreateUnreachable();
                return {llvm::PoisonValue::get(llvm::Type::getInt8Ty(context_)), true};
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
        case FirCallKind::CString:
            fail(span, "LLVM cString call reached ordinary call lowering");
            return {};
        case FirCallKind::SizeOf:
            if (!values.empty() || call.typeArguments.size() != 1) {
                fail(span, "LLVM sizeOf call has invalid arguments");
                return {};
            }
            if (auto *target = typeOf(call.typeArguments.front());
                target != nullptr && target->isSized()) {
                result = llvm::ConstantExpr::getSizeOf(target);
                if (result->getType() != sizeType()) {
                    result = builder_.CreateIntCast(result, sizeType(), false);
                }
                break;
            }
            fail(span, "LLVM sizeOf call has an unsized type");
            return {};
        case FirCallKind::PointerCast:
            if (values.size() != 1 || call.typeArguments.size() != 2) {
                fail(span, "LLVM pointerCast call has invalid arguments");
                return {};
            }
            result = values.front();
            break;
        case FirCallKind::NumericConversion:
            if (values.size() != 1 || call.typeArguments.size() != 2) {
                fail(span, "LLVM numeric conversion has invalid arguments");
                return {};
            }
            result = emitNumericConversion(values.front(), call.typeArguments[0],
                                           call.typeArguments[1], type, span);
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
                callable = builder_.CreateLoad(typeOf(callableTypeValue), callable);
            }
            auto *signature = callableType(callableTypeValue);
            if (signature == nullptr) {
                fail(span, "LLVM backend cannot lower this callable signature");
                return {};
            }
            if (isCFunction(callableTypeValue)) {
                result = builder_.CreateCall(signature, callable, values);
            } else {
                auto *environment = builder_.CreateExtractValue(callable, 0);
                auto *target = builder_.CreateExtractValue(callable, 1);
                values.insert(values.begin(), environment);
                result = builder_.CreateCall(signature, target, values);
            }
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
            auto *methodAddress = builder_.CreateStructGEP(
                contractVtableTypes_[call.contract], vtable, llvmIndex(call.method + 1));
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
        for (std::size_t index = 0;
             index < sourceValues.size() && index < call.argumentDrops.size(); ++index) {
            if (call.argumentDrops[index]) {
                dropValue(sourceValues[index], function_->expressions[call.arguments[index]].type);
            }
        }
        for (auto argument = arguments.rbegin(); argument != arguments.rend(); ++argument) {
            for (auto cleanup = argument->cleanups.rbegin(); cleanup != argument->cleanups.rend();
                 ++cleanup) {
                dropAddress(cleanup->address, cleanup->type);
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

    llvm::Value *emitUncheckedNumericConversion(llvm::Value *value, Type source, Type target,
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

    llvm::Value *emitFiniteCondition(llvm::Value *value) {
        auto *positiveInfinity = llvm::ConstantFP::getInfinity(value->getType());
        auto *negativeInfinity = llvm::ConstantFP::getInfinity(value->getType(), true);
        return builder_.CreateAnd(builder_.CreateFCmpOGT(value, negativeInfinity),
                                  builder_.CreateFCmpOLT(value, positiveInfinity));
    }

    llvm::Value *emitIntegerRangeCondition(llvm::Value *value, Type source, Type target,
                                           SourceSpan span) {
        auto *targetType = llvm::dyn_cast_or_null<llvm::IntegerType>(typeOf(target));
        if (!isInteger(source) || targetType == nullptr) {
            fail(span, "LLVM integer range check received invalid types");
            return nullptr;
        }
        auto *wideType = llvm::Type::getInt128Ty(context_);
        auto *wideValue = builder_.CreateIntCast(value, wideType, isSignedInteger(source));
        const auto bits = targetType->getBitWidth();
        const auto minimum = isSignedInteger(target)
                                 ? llvm::APInt::getSignedMinValue(bits).sext(128)
                                 : llvm::APInt(128, 0);
        const auto maximum = isSignedInteger(target)
                                 ? llvm::APInt::getSignedMaxValue(bits).sext(128)
                                 : llvm::APInt::getMaxValue(bits).zext(128);
        return builder_.CreateAnd(
            builder_.CreateICmpSGE(wideValue, llvm::ConstantInt::get(context_, minimum)),
            builder_.CreateICmpSLE(wideValue, llvm::ConstantInt::get(context_, maximum)));
    }

    llvm::Value *emitFloatIntegerRangeCondition(llvm::Value *value, Type target,
                                                SourceSpan span) {
        auto *targetType = llvm::dyn_cast_or_null<llvm::IntegerType>(typeOf(target));
        if (!isInteger(target) || targetType == nullptr) {
            fail(span, "LLVM floating range check received an invalid target type");
            return nullptr;
        }
        const auto bits = targetType->getBitWidth();
        const auto lower = isSignedInteger(target)
                               ? -std::ldexp(1.0, static_cast<int>(bits - 1))
                               : 0.0;
        const auto upper = std::ldexp(
            1.0, static_cast<int>(bits - (isSignedInteger(target) ? 1 : 0)));
        return builder_.CreateAnd(
            builder_.CreateFCmpOGE(value, llvm::ConstantFP::get(value->getType(), lower)),
            builder_.CreateFCmpOLT(value, llvm::ConstantFP::get(value->getType(), upper)));
    }

    llvm::Value *emitNumericConversion(llvm::Value *value, Type source, Type target,
                                       const Type &resultType, SourceSpan span) {
        if (resultType == target) {
            return emitUncheckedNumericConversion(value, source, target, span);
        }
        if (resultType.kind != TypeKind::Enum ||
            resultType.declaration >= program_.enums.size() ||
            program_.enums[resultType.declaration].name != "Result" ||
            program_.enums[resultType.declaration].variants.size() != 2 ||
            !program_.enums[resultType.declaration].variants[0].payload.has_value() ||
            !program_.enums[resultType.declaration].variants[1].payload.has_value()) {
            fail(span, "LLVM fallible numeric conversion has an invalid Result type");
            return nullptr;
        }
        const auto errorType = *program_.enums[resultType.declaration].variants[1].payload;
        if (errorType.kind != TypeKind::Enum || errorType.declaration >= program_.enums.size() ||
            program_.enums[errorType.declaration].name != "NumberError" ||
            program_.enums[errorType.declaration].variants.size() != 3) {
            fail(span, "LLVM fallible numeric conversion has an invalid NumberError type");
            return nullptr;
        }
        auto *layout = llvm::dyn_cast_or_null<llvm::StructType>(typeOf(resultType));
        if (layout == nullptr) {
            fail(span, "LLVM fallible numeric conversion has no Result layout");
            return nullptr;
        }

        auto *storage = createEntryAlloca(layout, "numeric.result");
        builder_.CreateStore(llvm::Constant::getNullValue(layout), storage);
        auto *done = llvm::BasicBlock::Create(context_, "numeric.done", llvmFunction_);
        const auto emitSuccess = [&](llvm::Value *converted) {
            auto *payload = enumPayloadAddress(storage, resultType, 0, span);
            if (payload != nullptr) {
                builder_.CreateStore(converted, payload);
                setEnumTag(storage, resultType, 0);
                builder_.CreateBr(done);
            }
        };
        const auto emitFailure = [&](FirVariantId variant) {
            auto *payload = enumPayloadAddress(storage, resultType, 1, span);
            if (payload != nullptr) {
                llvm::Value *error = llvm::Constant::getNullValue(typeOf(errorType));
                error = builder_.CreateInsertValue(
                    error, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), variant), 0);
                builder_.CreateStore(error, payload);
                setEnumTag(storage, resultType, 1);
                builder_.CreateBr(done);
            }
        };

        if (isInteger(source) && isInteger(target)) {
            auto *success = llvm::BasicBlock::Create(context_, "numeric.success", llvmFunction_);
            auto *outOfRange =
                llvm::BasicBlock::Create(context_, "numeric.out_of_range", llvmFunction_);
            auto *condition = emitIntegerRangeCondition(value, source, target, span);
            if (condition == nullptr) {
                return nullptr;
            }
            builder_.CreateCondBr(condition, success, outOfRange);
            builder_.SetInsertPoint(success);
            emitSuccess(emitUncheckedNumericConversion(value, source, target, span));
            builder_.SetInsertPoint(outOfRange);
            emitFailure(0);
        } else if (isFloating(source) && isInteger(target)) {
            auto *range = llvm::BasicBlock::Create(context_, "numeric.range", llvmFunction_);
            auto *convert = llvm::BasicBlock::Create(context_, "numeric.convert", llvmFunction_);
            auto *success = llvm::BasicBlock::Create(context_, "numeric.success", llvmFunction_);
            auto *nonFinite =
                llvm::BasicBlock::Create(context_, "numeric.non_finite", llvmFunction_);
            auto *outOfRange =
                llvm::BasicBlock::Create(context_, "numeric.out_of_range", llvmFunction_);
            auto *precisionLoss =
                llvm::BasicBlock::Create(context_, "numeric.precision_loss", llvmFunction_);
            builder_.CreateCondBr(emitFiniteCondition(value), range, nonFinite);
            builder_.SetInsertPoint(nonFinite);
            emitFailure(1);
            builder_.SetInsertPoint(range);
            auto *condition = emitFloatIntegerRangeCondition(value, target, span);
            if (condition == nullptr) {
                return nullptr;
            }
            builder_.CreateCondBr(condition, convert, outOfRange);
            builder_.SetInsertPoint(outOfRange);
            emitFailure(0);
            builder_.SetInsertPoint(convert);
            auto *converted = emitUncheckedNumericConversion(value, source, target, span);
            auto *roundTrip = emitUncheckedNumericConversion(converted, target, source, span);
            builder_.CreateCondBr(builder_.CreateFCmpOEQ(roundTrip, value), success,
                                  precisionLoss);
            builder_.SetInsertPoint(success);
            emitSuccess(converted);
            builder_.SetInsertPoint(precisionLoss);
            emitFailure(2);
        } else {
            auto *convert = llvm::BasicBlock::Create(context_, "numeric.convert", llvmFunction_);
            auto *check = llvm::BasicBlock::Create(context_, "numeric.check", llvmFunction_);
            auto *roundTrip = isInteger(source)
                                  ? llvm::BasicBlock::Create(context_, "numeric.round_trip",
                                                             llvmFunction_)
                                  : nullptr;
            auto *success = llvm::BasicBlock::Create(context_, "numeric.success", llvmFunction_);
            auto *nonFinite = isFloating(source)
                                  ? llvm::BasicBlock::Create(context_, "numeric.non_finite",
                                                             llvmFunction_)
                                  : nullptr;
            auto *outOfRange =
                llvm::BasicBlock::Create(context_, "numeric.out_of_range", llvmFunction_);
            auto *precisionLoss =
                llvm::BasicBlock::Create(context_, "numeric.precision_loss", llvmFunction_);
            if (isFloating(source)) {
                builder_.CreateCondBr(emitFiniteCondition(value), convert, nonFinite);
                builder_.SetInsertPoint(nonFinite);
                emitFailure(1);
            } else {
                builder_.CreateBr(convert);
            }
            builder_.SetInsertPoint(convert);
            auto *converted = emitUncheckedNumericConversion(value, source, target, span);
            builder_.CreateCondBr(emitFiniteCondition(converted), check, outOfRange);
            builder_.SetInsertPoint(outOfRange);
            emitFailure(0);
            builder_.SetInsertPoint(check);
            if (isInteger(source)) {
                auto *condition = emitFloatIntegerRangeCondition(converted, source, span);
                if (condition == nullptr) {
                    return nullptr;
                }
                builder_.CreateCondBr(condition, roundTrip, precisionLoss);
                builder_.SetInsertPoint(roundTrip);
                auto *restored =
                    emitUncheckedNumericConversion(converted, target, source, span);
                builder_.CreateCondBr(builder_.CreateICmpEQ(restored, value), success,
                                      precisionLoss);
            } else {
                auto *restored =
                    emitUncheckedNumericConversion(converted, target, source, span);
                builder_.CreateCondBr(builder_.CreateFCmpOEQ(restored, value), success,
                                      precisionLoss);
            }
            builder_.SetInsertPoint(success);
            emitSuccess(converted);
            builder_.SetInsertPoint(precisionLoss);
            emitFailure(2);
        }
        builder_.SetInsertPoint(done);
        return builder_.CreateLoad(layout, storage, "numeric.result.value");
    }

    EmittedValue emitConditional(const FirConditionalExpression &conditional, const Type &type,
                                 SourceSpan span) {
        const auto condition = emitExpression(conditional.condition);
        if (condition.diverges) {
            return condition;
        }
        auto *thenBlock = llvm::BasicBlock::Create(context_, "value.then", llvmFunction_);
        auto *elseBlock = llvm::BasicBlock::Create(context_, "value.else", llvmFunction_);
        auto *mergeBlock = llvm::BasicBlock::Create(context_, "value.end", llvmFunction_);
        builder_.CreateCondBr(condition.value, thenBlock, elseBlock);

        std::vector<std::pair<llvm::Value *, llvm::BasicBlock *>> incoming;
        auto hasMergeIncoming = false;
        const auto emitBranch = [&](FirBlockId block, FirExpressionId value) {
            if (block >= function_->blocks.size()) {
                fail(span, "LLVM backend received an invalid conditional branch");
                return;
            }
            for (const auto statement : function_->blocks[block].statements) {
                if (taskPoll_ && emitSuspendingStatement(statement)) {
                    if (builder_.GetInsertBlock()->getTerminator() != nullptr) {
                        return;
                    }
                    continue;
                }
                if (emitStatement(statement)) {
                    return;
                }
            }
            const auto result = emitExpression(value);
            if (result.diverges) {
                return;
            }
            if (type != voidType && result.value == nullptr) {
                fail(span, "LLVM conditional branch did not produce a value");
                builder_.CreateUnreachable();
                return;
            }
            dropLocals(function_->blocks[block].drops);
            builder_.CreateBr(mergeBlock);
            hasMergeIncoming = true;
            if (type != voidType) {
                incoming.emplace_back(result.value, builder_.GetInsertBlock());
            }
        };

        builder_.SetInsertPoint(thenBlock);
        emitBranch(conditional.thenBlock, conditional.thenValue);

        builder_.SetInsertPoint(elseBlock);
        emitBranch(conditional.elseBlock, conditional.elseValue);

        if (!hasMergeIncoming) {
            mergeBlock->eraseFromParent();
            return {llvm::PoisonValue::get(llvm::Type::getInt8Ty(context_)), true};
        }

        builder_.SetInsertPoint(mergeBlock);
        if (type == voidType) {
            return {nullptr, false};
        }
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
        auto *type =
            local < function_->locals.size() ? typeOf(function_->locals[local].type) : nullptr;
        if (type == nullptr || type->isVoidTy()) {
            fail(function_->sourceSpan, "LLVM backend cannot load this local");
            return nullptr;
        }
        return builder_.CreateLoad(type, locals_[local], "local.value");
    }

    void activateLocal(FirLocalId local) {
        if (!taskPoll_ || local >= localActive_.size() || localActive_[local] == nullptr ||
            builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        builder_.CreateStore(llvm::ConstantInt::getTrue(context_), localActive_[local]);
    }

    void deactivateLocal(FirLocalId local) {
        if (!taskPoll_ || local >= localActive_.size() || localActive_[local] == nullptr ||
            builder_.GetInsertBlock()->getTerminator() != nullptr) {
            return;
        }
        builder_.CreateStore(llvm::ConstantInt::getFalse(context_), localActive_[local]);
    }

    void dropLocal(FirLocalId local) {
        if (local >= function_->locals.size() || local >= locals_.size() ||
            locals_[local] == nullptr || !typeRequiresDrop(function_->locals[local].type)) {
            return;
        }
        if (!taskPoll_ || local >= localActive_.size() || localActive_[local] == nullptr) {
            dropAddress(locals_[local], function_->locals[local].type);
            return;
        }
        auto *drop = llvm::BasicBlock::Create(context_, "local.drop", llvmFunction_);
        auto *done = llvm::BasicBlock::Create(context_, "local.drop.end", llvmFunction_);
        builder_.CreateCondBr(
            builder_.CreateLoad(llvm::Type::getInt1Ty(context_), localActive_[local]), drop, done);
        builder_.SetInsertPoint(drop);
        dropAddress(locals_[local], function_->locals[local].type);
        builder_.CreateStore(llvm::ConstantInt::getFalse(context_), localActive_[local]);
        builder_.CreateBr(done);
        builder_.SetInsertPoint(done);
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
        if (const auto *index = std::get_if<FirIndexExpression>(&expression)) {
            return isPlaceExpression(index->base);
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

    unsigned int structFieldIndex(FirStructId type, FirFieldId field) const {
        return llvmIndex(field +
                         (type < program_.structs.size() &&
                                  program_.structs[type].dropFunction.has_value()
                              ? 1
                              : 0));
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
        return builder_.CreateStructGEP(enumTypes_[type.declaration], address,
                                        llvmIndex(variant + 1), "enum.payload.address");
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
        activateLocal(local);
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
            (type.kind == TypeKind::Function && !isCFunction(type)) ||
            type.kind == TypeKind::Parameter) {
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
    }

    unsigned int structFieldIndex(const Type &type, FirFieldId field) const {
        return llvmIndex(field +
                         (type.kind == TypeKind::Struct &&
                                  type.declaration < program_.structs.size() &&
                                  program_.structs[type.declaration].dropFunction.has_value()
                              ? 1
                              : 0));
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
        } else if (sequence.kind == TypeKind::Slice && sequence.arguments.size() == 1) {
            auto slice = emitExpression(index.base).value;
            data = builder_.CreateExtractValue(slice, 0);
            length = builder_.CreateExtractValue(slice, 1);
        } else {
            data = emitAddress(index.base);
        }
        const auto value = emitExpression(index.index);
        if (value.diverges) {
            return nullptr;
        }
        if (sequence.kind == TypeKind::Array && sequence.arguments.size() == 1) {
            length = llvm::ConstantInt::get(sizeType(), sequence.declaration);
            setLocation(span);
            auto *checked = builder_.CreateCall(
                runtimeFunction("fdn_bounds_check", sizeType(), {sizeType(), sizeType()}),
                {integerToSize(value.value), length});
            auto *array = llvm::cast<llvm::ArrayType>(typeOf(sequence));
            return builder_.CreateInBoundsGEP(
                array, data,
                {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0), checked});
        }
        if (sequence.kind == TypeKind::Slice && sequence.arguments.size() == 1) {
            setLocation(span);
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

    llvm::AllocaInst *createEntryAlloca(llvm::Type *type, const llvm::Twine &name) {
        auto &entry = llvmFunction_->getEntryBlock();
        llvm::IRBuilder<> entryBuilder(&entry, entry.begin());
        return entryBuilder.CreateAlloca(type, nullptr, name);
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

    void enterNativeFrame(std::string_view symbol, SourceSpan span) {
        const auto source =
            function_->sourcePath.empty() ? sourcePath_ : std::string_view(function_->sourcePath);
        auto *functionNameValue = builder_.CreateGlobalString(symbol, "frame.native.function");
        auto *sourceName = builder_.CreateGlobalString(source, "frame.native.source");
        builder_.CreateCall(
            runtimeFunction("fdn_frame_enter_native", llvm::Type::getVoidTy(context_),
                            {pointerType(), pointerType(), pointerType(),
                             llvm::Type::getInt32Ty(context_), llvm::Type::getInt32Ty(context_)}),
            {frame_, functionNameValue, sourceName,
             llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), span.line),
             llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), span.column)});
    }

    void leaveFrame() {
        builder_.CreateCall(
            runtimeFunction("fdn_frame_leave", llvm::Type::getVoidTy(context_), {pointerType()}),
            {frame_});
    }

    void setLocation(SourceSpan span) {
        if (currentSubprogram_ != nullptr) {
            builder_.SetCurrentDebugLocation(llvm::DILocation::get(
                context_, static_cast<unsigned>(span.line), static_cast<unsigned>(span.column),
                currentSubprogram_));
        }
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
        auto *owner = builder_.GetInsertBlock()->getParent();
        auto *drop = llvm::BasicBlock::Create(context_, "drop.own", owner);
        auto *done = llvm::BasicBlock::Create(context_, "drop.own.end", owner);
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

    void emitCompositeDropAddress(llvm::Value *address, const Type &type) {
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
                auto *owner = builder_.GetInsertBlock()->getParent();
                auto *drop = llvm::BasicBlock::Create(context_, "drop.struct", owner);
                auto *done = llvm::BasicBlock::Create(context_, "drop.struct.end", owner);
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
            auto *owner = builder_.GetInsertBlock()->getParent();
            auto *done = llvm::BasicBlock::Create(context_, "drop.enum.end", owner);
            auto *invalid = llvm::BasicBlock::Create(context_, "drop.enum.invalid", owner);
            auto *selection =
                builder_.CreateSwitch(tag, invalid, llvmIndex(declaration.variants.size()));
            for (std::size_t variant = 0; variant < declaration.variants.size(); ++variant) {
                auto *branch = llvm::BasicBlock::Create(context_, "drop.enum", owner);
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
        fail(function_->sourceSpan, "LLVM backend cannot emit a composite drop helper");
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
        if (type.kind == TypeKind::Function && !isCFunction(type)) {
            auto *value = builder_.CreateLoad(functionValueType_, address, "drop.function");
            auto *environment = builder_.CreateExtractValue(value, 0);
            auto *dropFunction = builder_.CreateExtractValue(value, 2);
            auto *owner = builder_.GetInsertBlock()->getParent();
            auto *drop = llvm::BasicBlock::Create(context_, "drop.closure", owner);
            auto *done = llvm::BasicBlock::Create(context_, "drop.closure.end", owner);
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
        if (type.kind == TypeKind::Own && type.arguments.size() == 1) {
            auto *value = builder_.CreateLoad(pointerType(), address, "drop.own.value");
            dropOwned(value, type);
            builder_.CreateStore(llvm::ConstantPointerNull::get(pointerType()), address);
            return;
        }
        if (type.kind == TypeKind::Task) {
            builder_.CreateCall(
                runtimeFunction("fdn_task_drop", llvm::Type::getVoidTy(context_), {pointerType()}),
                {address});
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
        if ((type.kind == TypeKind::Array && type.arguments.size() == 1) ||
            (type.kind == TypeKind::Struct && type.declaration < program_.structs.size()) ||
            (type.kind == TypeKind::Enum && type.declaration < program_.enums.size())) {
            builder_.CreateCall(declareDropHelper(type), {address});
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

    void dropInspectedTemporary(FirExpressionId id, const EmittedValue &expression) {
        for (auto cleanup = expression.cleanups.rbegin(); cleanup != expression.cleanups.rend();
             ++cleanup) {
            dropAddress(cleanup->address, cleanup->type);
        }
        if (id < function_->expressions.size() && !isPlaceExpression(id) &&
            typeRequiresDrop(function_->expressions[id].type)) {
            dropValue(expression.value, function_->expressions[id].type);
        }
    }

    void dropLocals(const std::vector<FirLocalId> &locals) {
        for (const auto local : locals) {
            dropLocal(local);
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
            const auto indirectResult = usesExternalResultPointer(function);
            auto *resultStorage = indirectResult ? wrapper->getArg(0) : nullptr;
            std::vector<llvm::Value *> arguments;
            arguments.reserve(function.parameters.size());
            for (std::size_t index = 0; index < function.parameters.size(); ++index) {
                const auto local = function.parameters[index];
                auto *value =
                    wrapper->getArg(llvmIndex(index + (indirectResult ? 1 : 0)));
                arguments.push_back(function.hasBody
                                        ? fromAbi(builder, value, function.locals[local].type)
                                        : toAbi(builder, value, function.locals[local].type));
            }
            auto invocationArguments = arguments;
            if (indirectResult) {
                invocationArguments.insert(invocationArguments.begin(), resultStorage);
            }
            auto *result = builder.CreateCall(target, invocationArguments);
            if (indirectResult) {
                result->addParamAttr(
                    0, llvm::Attribute::getWithStructRetType(context_, typeOf(function.returnType)));
            }
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
            } else if (indirectResult) {
                builder.CreateRetVoid();
            } else {
                builder.CreateRet(function.hasBody ? toAbi(builder, result, function.returnType)
                                                   : fromAbi(builder, result, function.returnType));
            }
        }
    }

    void emitMainWrapper() {
        const auto &entry = program_.functions[program_.main];
        const auto acceptsArguments = entry.parameters.size() == 1;
        if (entry.parameters.size() > 1) {
            fail(entry.sourceSpan, "LLVM entry point has an invalid parameter count");
            return;
        }
        std::vector<llvm::Type *> parameters;
        if (acceptsArguments) {
            parameters = {llvm::Type::getInt32Ty(context_), pointerType()};
        }
        auto *signature = llvm::FunctionType::get(llvm::Type::getInt32Ty(context_), parameters,
                                                  false);
        auto *main =
            llvm::Function::Create(signature, llvm::GlobalValue::ExternalLinkage, "main", module_);
        if (acceptsArguments) {
            main->getArg(0)->setName("argc");
            main->getArg(1)->setName("argv");
        }
        auto *block = llvm::BasicBlock::Create(context_, "entry", main);
        builder_.SetInsertPoint(block);
        if (debugBuilder_ != nullptr) {
            auto *file = debugFile(entry);
            std::vector<llvm::Metadata *> types{debugType(i32Type)};
            if (acceptsArguments) {
                types.push_back(debugType(i32Type));
                types.push_back(debugBuilder_->createUnspecifiedType("char **"));
            }
            auto *subprogram = debugBuilder_->createFunction(
                file, "main", "main", file, static_cast<unsigned>(entry.sourceSpan.line),
                debugBuilder_->createSubroutineType(
                    debugBuilder_->getOrCreateTypeArray(types)),
                static_cast<unsigned>(entry.sourceSpan.line), llvm::DINode::FlagPrototyped,
                llvm::DISubprogram::SPFlagDefinition |
                    (options_.optimize ? llvm::DISubprogram::SPFlagOptimized
                                       : llvm::DISubprogram::SPFlagZero));
            main->setSubprogram(subprogram);
            currentSubprogram_ = subprogram;
            builder_.SetCurrentDebugLocation(llvm::DILocation::get(
                context_, static_cast<unsigned>(entry.sourceSpan.line),
                static_cast<unsigned>(entry.sourceSpan.column), subprogram));
        } else {
            clearDebugLocation();
        }
        auto *entryFunction = functions_[program_.main];
        if (options_.entry.has_value()) {
            builder_.CreateCall(entryFunction);
            if (options_.verifyAllocations) {
                emitAllocationCheck();
            }
            builder_.CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0));
            return;
        }
        llvm::CallInst *result{};
        llvm::Value *argumentValues{};
        if (acceptsArguments) {
            const auto parameter = entry.parameters.front();
            if (parameter >= entry.locals.size()) {
                fail(entry.sourceSpan, "LLVM entry point has an invalid argument parameter");
                return;
            }
            auto *argumentType = llvm::dyn_cast_or_null<llvm::StructType>(
                typeOf(entry.locals[parameter].type));
            const auto argumentSize = module_.getDataLayout().getTypeAllocSize(stringType_);
            if (argumentType == nullptr || argumentSize.isScalable()) {
                fail(entry.sourceSpan, "LLVM entry point has an invalid argument layout");
                return;
            }

            auto *argc = main->getArg(0);
            auto *argv = main->getArg(1);
            auto *nativeCount = builder_.CreateSelect(
                builder_.CreateICmpSGT(
                    argc, llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
                builder_.CreateSub(argc,
                                   llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1)),
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0));
            auto *count = builder_.CreateIntCast(nativeCount, sizeType(), false,
                                                 "argument.count");
            const auto sizeBits = sizeType()->getIntegerBitWidth();
            const auto maximumCount =
                llvm::APInt::getMaxValue(sizeBits)
                    .udiv(llvm::APInt(sizeBits, argumentSize.getFixedValue()));
            auto *overflow = llvm::BasicBlock::Create(context_, "arguments.overflow", main);
            auto *choose = llvm::BasicBlock::Create(context_, "arguments.choose", main);
            builder_.CreateCondBr(
                builder_.CreateICmpUGT(count,
                                       llvm::ConstantInt::get(context_, maximumCount)),
                overflow, choose);
            builder_.SetInsertPoint(overflow);
            auto *message = builder_.CreateGlobalString("command-line argument count overflow",
                                                        "arguments.overflow.message");
            builder_.CreateCall(
                runtimeFunction("fdn_panic_cstr", llvm::Type::getVoidTy(context_),
                                {pointerType()}),
                {message});
            builder_.CreateUnreachable();

            auto *allocateArguments =
                llvm::BasicBlock::Create(context_, "arguments.allocate", main);
            auto *emptyArguments = llvm::BasicBlock::Create(context_, "arguments.empty", main);
            auto *argumentsReady = llvm::BasicBlock::Create(context_, "arguments.ready", main);
            builder_.SetInsertPoint(choose);
            builder_.CreateCondBr(
                builder_.CreateICmpNE(count, llvm::ConstantInt::get(sizeType(), 0)),
                allocateArguments, emptyArguments);
            builder_.SetInsertPoint(allocateArguments);
            auto *allocationBytes = builder_.CreateMul(
                count, llvm::ConstantInt::get(sizeType(), argumentSize.getFixedValue()));
            auto *allocated = builder_.CreateCall(
                runtimeFunction("fdn_alloc", pointerType(), {sizeType()}), {allocationBytes},
                "argument.values");
            builder_.CreateBr(argumentsReady);
            builder_.SetInsertPoint(emptyArguments);
            builder_.CreateBr(argumentsReady);
            builder_.SetInsertPoint(argumentsReady);
            auto *values = builder_.CreatePHI(pointerType(), 2, "argument.values");
            values->addIncoming(allocated, allocateArguments);
            values->addIncoming(llvm::ConstantPointerNull::get(pointerType()), emptyArguments);

            auto *loop = llvm::BasicBlock::Create(context_, "arguments.loop", main);
            auto *copy = llvm::BasicBlock::Create(context_, "arguments.copy", main);
            auto *copied = llvm::BasicBlock::Create(context_, "arguments.copied", main);
            builder_.CreateBr(loop);
            builder_.SetInsertPoint(loop);
            auto *index = builder_.CreatePHI(sizeType(), 2, "argument.index");
            index->addIncoming(llvm::ConstantInt::get(sizeType(), 0), argumentsReady);
            builder_.CreateCondBr(builder_.CreateICmpULT(index, count), copy, copied);
            builder_.SetInsertPoint(copy);
            auto *nativeIndex =
                builder_.CreateAdd(index, llvm::ConstantInt::get(sizeType(), 1));
            auto *argumentAddress =
                builder_.CreateInBoundsGEP(pointerType(), argv, nativeIndex);
            auto *argument = builder_.CreateLoad(pointerType(), argumentAddress);
            auto *length = builder_.CreateCall(
                runtimeFunction("strlen", sizeType(), {pointerType()}), {argument});
            llvm::Value *string = llvm::Constant::getNullValue(stringType_);
            string = builder_.CreateInsertValue(string, argument, 0);
            string = builder_.CreateInsertValue(string, length, 1);
            string = builder_.CreateInsertValue(
                string, llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), 0), 2);
            auto *target = builder_.CreateInBoundsGEP(stringType_, values, index);
            builder_.CreateStore(string, target);
            auto *next = builder_.CreateAdd(index, llvm::ConstantInt::get(sizeType(), 1));
            builder_.CreateBr(loop);
            index->addIncoming(next, copy);

            builder_.SetInsertPoint(copied);
            argumentValues = values;
            llvm::Value *arguments = llvm::Constant::getNullValue(argumentType);
            arguments = builder_.CreateInsertValue(arguments, values, 0);
            arguments = builder_.CreateInsertValue(arguments, count, 1);
            result = builder_.CreateCall(entryFunction, {arguments});
        } else {
            result = builder_.CreateCall(entryFunction);
        }
        if (argumentValues != nullptr) {
            builder_.CreateCall(
                runtimeFunction("fdn_dealloc", llvm::Type::getVoidTy(context_), {pointerType()}),
                {argumentValues});
        }
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
    llvm::StructType *selectCaseType_{};
    std::unique_ptr<llvm::DIBuilder> debugBuilder_;
    llvm::DIFile *primaryDebugFile_{};
    llvm::DISubprogram *currentSubprogram_{};
    std::unordered_map<std::string, llvm::DIFile *> debugFiles_;
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
    std::vector<ChannelDrop> channelDrops_;
    std::vector<DropHelper> dropHelpers_;
    std::vector<std::optional<TaskAdapter>> taskAdapters_;
    const FirFunction *function_{};
    FirFunctionId functionId_{};
    llvm::Function *llvmFunction_{};
    llvm::AllocaInst *frame_{};
    std::vector<llvm::Value *> locals_;
    std::vector<llvm::Value *> captureAddresses_;
    std::vector<llvm::Value *> localActive_;
    std::vector<LoopTarget> loops_;
    bool taskPoll_{};
    TaskAdapter *taskAdapter_{};
    llvm::Value *taskFrame_{};
    llvm::Value *taskPreviousCancellation_{};
    std::vector<llvm::BasicBlock *> taskStateBlocks_;
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
