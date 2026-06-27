#include "foundation/metadata.hpp"

#include <sstream>
#include <set>
#include <string>
#include <string_view>

namespace foundation {

namespace {

void emitString(std::ostringstream &out, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (const auto byte : value) {
        const auto valueByte = static_cast<unsigned char>(byte);
        if (byte == '"' || byte == '\\') {
            out << '\\' << byte;
        } else if (byte == '\b') {
            out << "\\b";
        } else if (byte == '\f') {
            out << "\\f";
        } else if (byte == '\n') {
            out << "\\n";
        } else if (byte == '\r') {
            out << "\\r";
        } else if (byte == '\t') {
            out << "\\t";
        } else if (valueByte < 0x20) {
            out << "\\u00" << hex[valueByte >> 4] << hex[valueByte & 0x0f];
        } else {
            out << byte;
        }
    }
    out << '"';
}

const char *targetName(FirAttributeTarget target) {
    switch (target) {
    case FirAttributeTarget::Function:
        return "fn";
    case FirAttributeTarget::Struct:
        return "struct";
    case FirAttributeTarget::Service:
        return "service";
    case FirAttributeTarget::Enum:
        return "enum";
    case FirAttributeTarget::Contract:
        return "contract";
    case FirAttributeTarget::Method:
        return "method";
    case FirAttributeTarget::Action:
        return "action";
    case FirAttributeTarget::Field:
        return "field";
    case FirAttributeTarget::Variant:
        return "variant";
    case FirAttributeTarget::Parameter:
        return "parameter";
    }
    return "unknown";
}

std::string typeName(const FirProgram &program, const Type &type) {
    if (isMachineScalar(type)) {
        return foundation::typeName(type);
    }
    if (type == stringType) {
        return "String";
    }
    if (type.kind == TypeKind::Parameter) {
        return "$" + std::to_string(type.declaration);
    }
    if (type.kind == TypeKind::Array && type.arguments.size() == 1) {
        return "[" + std::to_string(type.declaration) + "]" +
               typeName(program, type.arguments.front());
    }
    if (type.kind == TypeKind::Slice && type.arguments.size() == 1) {
        return "[" + typeName(program, type.arguments.front()) + "]";
    }
    if ((type.kind == TypeKind::Raw || type.kind == TypeKind::RawConst) &&
        type.arguments.size() == 1) {
        return std::string(type.kind == TypeKind::Raw ? "*" : "*const ") +
               typeName(program, type.arguments.front());
    }
    std::string result;
    if (type.kind == TypeKind::Own) {
        result = "own ";
    } else if (type.kind == TypeKind::View) {
        result = "view ";
    } else if (type.kind == TypeKind::Edit) {
        result = "edit ";
    } else if (type.kind == TypeKind::Struct && type.declaration < program.structs.size()) {
        result = program.structs[type.declaration].name;
    } else if (type.kind == TypeKind::Enum && type.declaration < program.enums.size()) {
        result = program.enums[type.declaration].name;
    } else if (type.kind == TypeKind::Contract && type.declaration < program.contracts.size()) {
        result = program.contracts[type.declaration].name;
    } else {
        result = foundation::typeName(type);
    }
    if ((type.kind == TypeKind::Own || type.kind == TypeKind::View ||
         type.kind == TypeKind::Edit) &&
        type.arguments.size() == 1) {
        return result + typeName(program, type.arguments.front());
    }
    if (!type.arguments.empty()) {
        result += '<';
        for (std::size_t index = 0; index < type.arguments.size(); ++index) {
            if (index != 0) {
                result += ',';
            }
            result += typeName(program, type.arguments[index]);
        }
        result += '>';
    }
    return result;
}

void emitValue(std::ostringstream &out, const FirProgram &program,
               const FirAttributeValue &value) {
    switch (value.kind) {
    case FirAttributeValueKind::Integer:
        if (value.negative) {
            out << '-';
        }
        out << value.magnitude;
        return;
    case FirAttributeValueKind::Floating:
        out << value.text;
        return;
    case FirAttributeValueKind::Boolean:
        out << (value.boolean ? "true" : "false");
        return;
    case FirAttributeValueKind::String:
        emitString(out, value.text);
        return;
    case FirAttributeValueKind::Enum: {
        out << "{\"case\":";
        std::string name = typeName(program, value.type);
        if (value.type.kind == TypeKind::Enum && value.type.declaration < program.enums.size() &&
            value.variant < program.enums[value.type.declaration].variants.size()) {
            name += '.' + program.enums[value.type.declaration].variants[value.variant].name;
        }
        emitString(out, name);
        if (!value.children.empty()) {
            out << ",\"value\":";
            emitValue(out, program, value.children.front());
        }
        out << '}';
        return;
    }
    case FirAttributeValueKind::Array:
        out << '[';
        for (std::size_t index = 0; index < value.children.size(); ++index) {
            if (index != 0) {
                out << ',';
            }
            emitValue(out, program, value.children[index]);
        }
        out << ']';
        return;
    case FirAttributeValueKind::Struct:
        out << "{\"type\":";
        emitString(out, typeName(program, value.type));
        out << ",\"fields\":{";
        for (std::size_t index = 0; index < value.children.size(); ++index) {
            if (index != 0) {
                out << ',';
            }
            emitString(out, value.members[index]);
            out << ':';
            emitValue(out, program, value.children[index]);
        }
        out << "}}";
        return;
    }
}

void emitUses(std::ostringstream &out, const FirProgram &program,
              const std::vector<FirAttributeUse> &uses) {
    out << '[';
    for (std::size_t index = 0; index < uses.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto &use = uses[index];
        out << "{\"name\":";
        if (use.declaration < program.attributeDeclarations.size()) {
            emitString(out, program.attributeDeclarations[use.declaration].name);
        } else {
            emitString(out, "<invalid>");
        }
        out << ",\"arguments\":{";
        for (std::size_t argument = 0; argument < use.arguments.size(); ++argument) {
            if (argument != 0) {
                out << ',';
            }
            emitString(out, use.arguments[argument].name);
            out << ':';
            emitValue(out, program, use.arguments[argument].value);
        }
        out << "}}";
    }
    out << ']';
}

void emitDeclaration(std::ostringstream &out, const FirProgram &program, bool &first,
                     std::string_view id, std::string_view kind,
                     const std::vector<FirAttributeUse> &attributes, bool force = false) {
    if (attributes.empty() && !force) {
        return;
    }
    if (!first) {
        out << ',';
    }
    first = false;
    out << "{\"id\":";
    emitString(out, id);
    out << ",\"kind\":";
    emitString(out, kind);
    out << ",\"attributes\":";
    emitUses(out, program, attributes);
    out << '}';
}

void emitStateTransitionDeclaration(std::ostringstream &out, const FirProgram &program,
                                    bool &first, const FirFunction &function) {
    if (!function.stateTransition.has_value() || function.parameters.empty()) {
        return;
    }
    const auto receiver = function.parameters.front();
    if (receiver >= function.locals.size()) {
        return;
    }
    const auto &receiverType = function.locals[receiver].type;
    if (receiverType.kind != TypeKind::Edit || receiverType.arguments.size() != 1 ||
        receiverType.arguments.front().kind != TypeKind::Enum ||
        receiverType.arguments.front().declaration >= program.enums.size()) {
        return;
    }
    const auto machine = receiverType.arguments.front().declaration;
    const auto &declaration = program.enums[machine];
    const auto &transition = *function.stateTransition;
    if (!first) {
        out << ',';
    }
    first = false;
    const auto separator = function.name.rfind('.');
    const auto event = separator == std::string::npos
                           ? function.name
                           : function.name.substr(separator + 1);
    out << "{\"id\":";
    emitString(out, declaration.name + "#transition:" + event);
    out << ",\"kind\":\"transition\",\"owner\":";
    emitString(out, declaration.name);
    out << ",\"sources\":[";
    for (std::size_t index = 0; index < transition.sourceVariants.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto variant = transition.sourceVariants[index];
        emitString(out, variant < declaration.variants.size()
                            ? declaration.variants[variant].name
                            : std::string{});
    }
    out << "],\"destination\":";
    emitString(out, transition.destinationVariant < declaration.variants.size()
                        ? declaration.variants[transition.destinationVariant].name
                        : std::string{});
    if (transition.destinationParameter.has_value() &&
        *transition.destinationParameter < function.locals.size()) {
        out << ",\"payloadParameter\":";
        emitString(out, function.locals[*transition.destinationParameter].name);
    }
    out << ",\"attributes\":";
    emitUses(out, program, function.attributes);
    out << '}';
}

void emitWorkflowDeclaration(std::ostringstream &out, const FirProgram &program,
                             bool &first, const FirFunction &function) {
    if (!function.workflow.has_value()) {
        return;
    }
    const auto &workflow = *function.workflow;
    if (!first) {
        out << ',';
    }
    first = false;
    out << "{\"id\":";
    emitString(out, function.name);
    out << ",\"kind\":";
    emitString(out, workflow.kind == FirWorkflowKind::Pipeline ? "pipeline" : "saga");
    out << ",\"input\":";
    emitString(out, typeName(program, workflow.inputType));
    out << ",\"output\":";
    emitString(out, typeName(program, workflow.successType));
    out << ",\"error\":";
    emitString(out, typeName(program, workflow.errorType));
    out << ",\"callableError\":";
    emitString(out, typeName(program, workflow.failureType));
    out << ",\"steps\":[";
    for (std::size_t index = 0; index < workflow.steps.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto &step = workflow.steps[index];
        out << "{\"name\":";
        emitString(out, step.name);
        out << ",\"function\":";
        emitString(out, step.function < program.functions.size()
                            ? program.functions[step.function].name
                            : std::string{});
        out << ",\"attempts\":" << step.attempts;
        if (step.compensation.has_value()) {
            out << ",\"compensation\":";
            emitString(out, *step.compensation < program.functions.size()
                                ? program.functions[*step.compensation].name
                                : std::string{});
        }
        out << '}';
    }
    out << "],\"attributes\":";
    emitUses(out, program, function.attributes);
    out << '}';
}

std::string_view attributePackage(std::string_view name) {
    const auto separator = name.rfind('.');
    return separator == std::string_view::npos ? std::string_view{} : name.substr(0, separator);
}

void collectAttributePackages(const FirProgram &program,
                              const std::vector<FirAttributeUse> &uses,
                              std::set<std::string> &packages) {
    for (const auto &use : uses) {
        if (use.declaration < program.attributeDeclarations.size()) {
            packages.emplace(attributePackage(
                program.attributeDeclarations[use.declaration].name));
        }
    }
}

} // namespace

std::string emitMetadata(const FirProgram &program) {
    std::set<std::string> attributePackages;
    for (const auto &type : program.structs) {
        collectAttributePackages(program, type.attributes, attributePackages);
        for (const auto &field : type.fields) {
            collectAttributePackages(program, field.attributes, attributePackages);
        }
    }
    for (const auto &type : program.enums) {
        collectAttributePackages(program, type.attributes, attributePackages);
        for (const auto &variant : type.variants) {
            collectAttributePackages(program, variant.attributes, attributePackages);
        }
    }
    for (const auto &type : program.contracts) {
        collectAttributePackages(program, type.attributes, attributePackages);
        for (const auto &method : type.methods) {
            collectAttributePackages(program, method.attributes, attributePackages);
            for (const auto &parameter : method.parameterAttributes) {
                collectAttributePackages(program, parameter, attributePackages);
            }
        }
    }
    for (const auto &function : program.functions) {
        collectAttributePackages(program, function.attributes, attributePackages);
        for (const auto &parameter : function.parameterAttributes) {
            collectAttributePackages(program, parameter, attributePackages);
        }
    }

    std::ostringstream out;
    out << "{\"schema\":\"foundation.metadata/v1\",\"attributes\":[";
    auto firstAttribute = true;
    for (std::size_t index = 0; index < program.attributeDeclarations.size(); ++index) {
        const auto &attribute = program.attributeDeclarations[index];
        if (!attributePackages.contains(
                std::string(attributePackage(attribute.name)))) {
            continue;
        }
        if (!firstAttribute) {
            out << ',';
        }
        firstAttribute = false;
        out << "{\"name\":";
        emitString(out, attribute.name);
        out << ",\"exported\":" << (attribute.exported ? "true" : "false")
            << ",\"repeatable\":" << (attribute.repeatable ? "true" : "false")
            << ",\"targets\":[";
        for (std::size_t target = 0; target < attribute.targets.size(); ++target) {
            if (target != 0) {
                out << ',';
            }
            emitString(out, targetName(attribute.targets[target]));
        }
        out << "],\"parameters\":[";
        for (std::size_t parameter = 0; parameter < attribute.parameters.size(); ++parameter) {
            if (parameter != 0) {
                out << ',';
            }
            out << "{\"name\":";
            emitString(out, attribute.parameters[parameter].name);
            out << ",\"type\":";
            emitString(out, typeName(program, attribute.parameters[parameter].type));
            out << '}';
        }
        out << "]}";
    }
    out << "],\"declarations\":[";
    auto first = true;
    for (const auto &type : program.structs) {
        emitDeclaration(out, program, first, type.name, type.service ? "service" : "struct",
                        type.attributes, type.service);
        for (const auto &field : type.fields) {
            emitDeclaration(out, program, first, type.name + "#field:" + field.name, "field",
                            field.attributes);
        }
    }
    for (const auto &type : program.enums) {
        emitDeclaration(out, program, first, type.name,
                        type.stateMachine ? "state_machine" : "enum", type.attributes,
                        type.stateMachine);
        for (const auto &variant : type.variants) {
            emitDeclaration(out, program, first, type.name + "#variant:" + variant.name,
                            type.stateMachine ? "state" : "variant", variant.attributes,
                            type.stateMachine);
        }
    }
    for (const auto &type : program.contracts) {
        emitDeclaration(out, program, first, type.name, "contract", type.attributes);
        for (const auto &method : type.methods) {
            const auto id = type.name + "#method:" + method.name;
            emitDeclaration(out, program, first, id, "method", method.attributes);
            for (std::size_t parameter = 0; parameter < method.parameterAttributes.size();
                 ++parameter) {
                emitDeclaration(out, program, first,
                                id + "#parameter:" + method.parameterNames[parameter],
                                "parameter",
                                method.parameterAttributes[parameter]);
            }
        }
    }
    for (const auto &function : program.functions) {
        if (function.stateTransition.has_value()) {
            emitStateTransitionDeclaration(out, program, first, function);
            continue;
        }
        if (function.workflow.has_value()) {
            emitWorkflowDeclaration(out, program, first, function);
            continue;
        }
        const auto kind = function.action                 ? "action"
                          : function.method               ? "method"
                                                          : "fn";
        emitDeclaration(out, program, first, function.name, kind, function.attributes,
                        function.action || function.stateTransition.has_value());
        for (std::size_t parameter = 0; parameter < function.parameterAttributes.size();
             ++parameter) {
            const auto local = function.parameters[parameter];
            emitDeclaration(out, program, first,
                            function.name + "#parameter:" + function.locals[local].name,
                            "parameter", function.parameterAttributes[parameter]);
        }
    }
    out << "]}\n";
    return out.str();
}

} // namespace foundation
