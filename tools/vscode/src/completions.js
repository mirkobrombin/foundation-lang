"use strict";

const staticCompletions = [
    { label: "package", kind: "Keyword" },
    { label: "import", kind: "Keyword" },
    { label: "as", kind: "Keyword" },
    { label: "extern", kind: "Keyword", detail: "Declare a C ABI import or export" },
    { label: "c", kind: "Value", detail: "C application binary interface" },
    { label: "struct", kind: "Keyword" },
    { label: "enum", kind: "Keyword" },
    { label: "contract", kind: "Keyword" },
    { label: "implements", kind: "Keyword" },
    { label: "fn", kind: "Keyword" },
    { label: "let", kind: "Keyword" },
    { label: "var", kind: "Keyword" },
    { label: "return", kind: "Keyword" },
    { label: "discard", kind: "Keyword" },
    { label: "if", kind: "Keyword" },
    { label: "else", kind: "Keyword" },
    { label: "while", kind: "Keyword" },
    { label: "match", kind: "Keyword" },
    { label: "capture", kind: "Keyword", detail: "Declare explicit closure captures" },
    { label: "replace", kind: "Keyword", detail: "Replace a mutable place and return its previous value" },
    { label: "with", kind: "Keyword", detail: "Introduce a replacement value" },
    { label: "own", kind: "Keyword", detail: "Create or declare an exclusive owner" },
    { label: "view", kind: "Keyword", detail: "Create or declare a shared borrow" },
    { label: "edit", kind: "Keyword", detail: "Create or declare an exclusive mutable borrow" },
    { label: "true", kind: "Constant" },
    { label: "false", kind: "Constant" },
    { label: "i32", kind: "TypeParameter" },
    { label: "bool", kind: "TypeParameter" },
    { label: "String", kind: "TypeParameter" },
    { label: "void", kind: "TypeParameter" },
    { label: "Option", kind: "TypeParameter", detail: "primitive Option<T>" },
    { label: "Result", kind: "TypeParameter", detail: "primitive Result<T, E>" },
    {
        label: "fn(...) R",
        kind: "TypeParameter",
        detail: "function value type",
        insertText: "fn(${1:parameters}) ${2:R}"
    },
    {
        label: "[N]T",
        kind: "TypeParameter",
        detail: "fixed array type",
        insertText: "[${1:N}]${2:T}"
    },
    {
        label: "view [T]",
        kind: "TypeParameter",
        detail: "shared slice type",
        insertText: "view [${1:T}]"
    },
    {
        label: "edit [T]",
        kind: "TypeParameter",
        detail: "mutable slice type",
        insertText: "edit [${1:T}]"
    },
    { label: ".None", kind: "EnumMember", insertText: ".None" },
    { label: ".Some", kind: "EnumMember", insertText: ".Some(${1:value})" },
    { label: ".Ok", kind: "EnumMember", insertText: ".Ok(${1:value})" },
    { label: ".Err", kind: "EnumMember", insertText: ".Err(${1:error})" },
    { label: "Option.None", kind: "EnumMember", insertText: "Option<${1:T}>.None" },
    { label: "Option.Some", kind: "EnumMember", insertText: "Option<${1:T}>.Some(${2:value})" },
    { label: "Result.Ok", kind: "EnumMember", insertText: "Result<${1:T}, ${2:E}>.Ok(${3:value})" },
    { label: "Result.Err", kind: "EnumMember", insertText: "Result<${1:T}, ${2:E}>.Err(${3:error})" },
    {
        label: "print",
        kind: "Function",
        detail: "builtin fn print(value String) void",
        insertText: "print(${1:value})"
    },
    {
        label: "panic",
        kind: "Function",
        detail: "builtin fn panic(value String) void",
        insertText: "panic(${1:message})"
    }
];

function maskTrivia(source) {
    let masked = "";
    let offset = 0;

    while (offset < source.length) {
        if (source[offset] === "/" && source[offset + 1] === "/") {
            while (offset < source.length && source[offset] !== "\n") {
                masked += " ";
                offset += 1;
            }
            continue;
        }

        if (source[offset] === "\"") {
            masked += " ";
            offset += 1;
            while (offset < source.length) {
                const current = source[offset];
                masked += current === "\n" ? "\n" : " ";
                offset += 1;
                if (current === "\\" && offset < source.length) {
                    masked += source[offset] === "\n" ? "\n" : " ";
                    offset += 1;
                    continue;
                }
                if (current === "\"") {
                    break;
                }
            }
            continue;
        }

        masked += source[offset];
        offset += 1;
    }

    return masked;
}

function splitTopLevel(source, separator) {
    const parts = [];
    let start = 0;
    let depth = 0;
    for (let offset = 0; offset < source.length; offset += 1) {
        if (source[offset] === "<") {
            depth += 1;
        } else if (source[offset] === ">") {
            depth = Math.max(0, depth - 1);
        } else if (source[offset] === separator && depth === 0) {
            parts.push(source.slice(start, offset));
            start = offset + 1;
        }
    }
    parts.push(source.slice(start));
    return parts;
}

function collectTypeParameters(source) {
    if (!source) {
        return [];
    }
    return splitTopLevel(source, ",")
        .map((parameter) => parameter.trim())
        .filter((parameter) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(parameter));
}

function collectParameters(source) {
    return splitTopLevel(source, ",")
        .map((parameter) => parameter.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s+[A-Za-z_\[]/))
        .filter(Boolean)
        .map((parameter) => parameter[1]);
}

function collectBracedDeclarations(source, keyword) {
    const declarations = [];
    const header = new RegExp(
        `\\b${keyword}\\s+([A-Za-z_][A-Za-z0-9_]*)(?:\\s*<([^>{}]*)>)?` +
        "(?:\\s+implements\\s+([^{}]+?))?\\s*\\{",
        "g"
    );
    let match;
    while ((match = header.exec(source)) !== null) {
        const open = header.lastIndex - 1;
        let depth = 1;
        let offset = open + 1;
        while (offset < source.length && depth !== 0) {
            if (source[offset] === "{") {
                depth += 1;
            } else if (source[offset] === "}") {
                depth -= 1;
            }
            offset += 1;
        }
        declarations.push({
            name: match[1],
            typeParameters: collectTypeParameters(match[2]),
            implementations: match[3] || "",
            body: source.slice(open + 1, depth === 0 ? offset - 1 : source.length),
            start: match.index,
            end: offset
        });
        header.lastIndex = offset;
    }
    return declarations;
}

function topLevelDepths(source) {
    const depths = new Uint16Array(source.length + 1);
    let depth = 0;
    for (let offset = 0; offset < source.length; offset += 1) {
        depths[offset] = depth;
        if (source[offset] === "{") {
            depth += 1;
        } else if (source[offset] === "}") {
            depth = Math.max(0, depth - 1);
        }
    }
    depths[source.length] = depth;
    return depths;
}

function collectMethods(source, owner, contract = false) {
    return [...source.matchAll(
        /\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/g
    )].map((match) => ({
        name: match[1],
        kind: "Method",
        owner,
        contract,
        parameters: collectParameters(match[2])
    }));
}

function topLevelSurface(source) {
    let result = "";
    let depth = 0;
    for (const character of source) {
        if (character === "{") {
            depth += 1;
            result += " ";
        } else if (character === "}") {
            depth = Math.max(0, depth - 1);
            result += " ";
        } else {
            result += depth === 0 || character === "\n" ? character : " ";
        }
    }
    return result;
}

function skipType(tokens, offset) {
    if (["own", "view", "edit"].includes(tokens[offset])) {
        offset += 1;
    }
    if (tokens[offset] === "fn") {
        offset += 1;
        if (tokens[offset] === "(") {
            let depth = 0;
            do {
                if (tokens[offset] === "(") {
                    depth += 1;
                } else if (tokens[offset] === ")") {
                    depth -= 1;
                }
                offset += 1;
            } while (offset < tokens.length && depth !== 0);
        }
        return skipType(tokens, offset);
    }
    if (tokens[offset] === "[") {
        offset += 1;
        if (/^[0-9]+$/.test(tokens[offset] || "")) {
            offset += 1;
            if (tokens[offset] === "]") {
                offset += 1;
            }
            return skipType(tokens, offset);
        }
        offset = skipType(tokens, offset);
        return tokens[offset] === "]" ? offset + 1 : offset;
    }
    offset += 1;
    if (tokens[offset] !== "<") {
        return offset;
    }
    let depth = 0;
    do {
        if (tokens[offset] === "<") {
            depth += 1;
        } else if (tokens[offset] === ">") {
            depth -= 1;
        }
        offset += 1;
    } while (offset < tokens.length && depth !== 0);
    return offset;
}

function collectStructFields(source) {
    const surface = topLevelSurface(source).replace(
        /\bfn\s+[A-Za-z_][A-Za-z0-9_]*\s*\([^)]*\)\s+[^\n{]+/g,
        ""
    );
    const tokens = [...surface.matchAll(/[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*|[0-9]+|[<>,()[\]]/g)]
        .map((match) => match[0]);
    const fields = [];
    let offset = 0;
    while (offset + 1 < tokens.length) {
        fields.push(tokens[offset]);
        offset += 1;
        offset = skipType(tokens, offset);
    }
    return fields;
}

function collectPackageDeclarations(source) {
    const masked = maskTrivia(source);
    const depths = topLevelDepths(masked);
    const packageMatch = masked.match(/\bpackage\s+([A-Za-z_][A-Za-z0-9_.]*)/);
    const imported = [...masked.matchAll(
        /\bimport\s+([A-Za-z_][A-Za-z0-9_.]*)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?/g
    )].map((match) => ({
        packageName: match[1],
        alias: match[2] || match[1].split(".").at(-1)
    }));
    const functions = [...masked.matchAll(
        /\bfn\s+([A-Z][A-Za-z0-9_]*)(?:\s*<([^>{}]*)>)?\s*\(([^)]*)\)/g
    )].filter((match) => depths[match.index] === 0).map((match) => ({
        name: match[1],
        kind: "Function",
        typeParameters: collectTypeParameters(match[2]),
        parameters: collectParameters(match[3])
    }));
    const structs = collectBracedDeclarations(masked, "struct")
        .filter((declaration) => /^[A-Z]/.test(declaration.name))
        .map((declaration) => ({
        name: declaration.name,
        kind: "Struct",
        typeParameters: declaration.typeParameters,
        methods: collectMethods(declaration.body, declaration.name)
            .filter((method) => /^[A-Z]/.test(method.name))
    }));
    const enums = collectBracedDeclarations(masked, "enum")
        .filter((declaration) => /^[A-Z]/.test(declaration.name))
        .map((declaration) => ({
        name: declaration.name,
        kind: "Enum",
        typeParameters: declaration.typeParameters,
        variants: [...declaration.body.matchAll(
            /(?:^|\s)([A-Z][A-Za-z0-9_]*)(?:\s*\(\s*([^)]*)\))?/g
        )].map((variant) => ({ name: variant[1], payload: Boolean(variant[2]) }))
    }));
    const contracts = collectBracedDeclarations(masked, "contract")
        .filter((declaration) => /^[A-Z]/.test(declaration.name))
        .map((declaration) => ({
            name: declaration.name,
            kind: "Contract",
            typeParameters: declaration.typeParameters,
            methods: collectMethods(declaration.body, declaration.name, true)
                .filter((method) => /^[A-Z]/.test(method.name))
        }));

    return {
        packageName: packageMatch ? packageMatch[1] : null,
        imports: imported,
        declarations: [...functions, ...structs, ...enums, ...contracts]
    };
}

function importedCompletions(source, projectSources) {
    const current = collectPackageDeclarations(source);
    const packages = new Map();
    for (const projectSource of projectSources) {
        const parsed = collectPackageDeclarations(projectSource);
        if (!parsed.packageName) {
            continue;
        }
        const declarations = packages.get(parsed.packageName) || [];
        declarations.push(...parsed.declarations);
        packages.set(parsed.packageName, declarations);
    }

    const completions = [...packages.keys()].map((packageName) => ({
        label: packageName,
        kind: "Module",
        detail: "Foundation package"
    }));
    for (const imported of current.imports) {
        completions.push({
            label: imported.alias,
            kind: "Module",
            detail: `Alias for ${imported.packageName}`
        });
        for (const declaration of packages.get(imported.packageName) || []) {
            const label = `${imported.alias}.${declaration.name}`;
            const typeArguments = declaration.typeParameters
                .map((parameter, index) => `\${${index + 1}:${parameter}}`)
                .join(", ");
            const qualified = `${label}${typeArguments ? `<${typeArguments}>` : ""}`;
            if (declaration.kind === "Function") {
                const offset = declaration.typeParameters.length;
                const argumentsText = declaration.parameters
                    .map((name, index) => `\${${offset + index + 1}:${name}}`)
                    .join(", ");
                completions.push({
                    label,
                    kind: "Function",
                    detail: `Exported function from ${imported.packageName}`,
                    insertText: `${qualified}(${argumentsText})`
                });
                continue;
            }
            completions.push({
                label,
                kind: declaration.kind,
                detail: `Exported ${declaration.kind.toLowerCase()} from ${imported.packageName}`,
                insertText: qualified
            });
            for (const method of declaration.methods || []) {
                completions.push({
                    label: method.name,
                    kind: "Method",
                    detail: `Exported method of ${declaration.name} from ${imported.packageName}`,
                    insertText: `${method.name}(${method.parameters.map((name, index) =>
                        `\${${index + 1}:${name}}`).join(", ")})`
                });
            }
            if (declaration.kind === "Enum") {
                for (const variant of declaration.variants) {
                    completions.push({
                        label: `${label}.${variant.name}`,
                        kind: "EnumMember",
                        detail: `Exported variant from ${imported.packageName}`,
                        insertText: variant.payload
                            ? `${qualified}.${variant.name}(\${${declaration.typeParameters.length + 1}:value})`
                            : `${qualified}.${variant.name}`
                    });
                }
            }
        }
    }
    return completions;
}

function collectCompletions(source, projectSources = []) {
    const masked = maskTrivia(source);
    const depths = topLevelDepths(masked);
    const completions = [...staticCompletions, ...importedCompletions(source, projectSources)];
    const functions = /\bfn\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*<([^>{}]*)>)?\s*\(([^)]*)\)/g;
    const structs = collectBracedDeclarations(masked, "struct");
    const enums = collectBracedDeclarations(masked, "enum");
    const contracts = collectBracedDeclarations(masked, "contract");
    const bindings = /\b(?:let|var)\s+([A-Za-z_][A-Za-z0-9_]*)/g;
    const structPatterns = /\blet\s+[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*\s*\{([^}]*)\}\s*=/g;
    let match;

    for (const declaration of structs) {
        const name = declaration.name;
        const typeParameters = declaration.typeParameters;
        const fields = collectStructFields(declaration.body);
        completions.push({
            label: name,
            kind: "Struct",
            detail: typeParameters.length === 0
                ? "Foundation struct"
                : `Foundation struct<${typeParameters.join(", ")}>`,
            insertText: `${name} { ${fields.map((field, index) => `${field} = \${${index + 1}:${field}}`).join(" ")} }`
        });
        for (const parameter of typeParameters) {
            completions.push({ label: parameter, kind: "TypeParameter", detail: `Type parameter of ${name}` });
        }
        for (const field of fields) {
            completions.push({
                label: field,
                kind: "Field",
                detail: `Field of ${name}`
            });
        }
        for (const method of collectMethods(declaration.body, name)) {
            completions.push({
                label: method.name,
                kind: "Method",
                detail: `Method of ${name}`,
                insertText: `${method.name}(${method.parameters.map((parameter, index) =>
                    `\${${index + 1}:${parameter}}`).join(", ")})`
            });
        }
    }

    for (const declaration of enums) {
        const name = declaration.name;
        const typeParameters = declaration.typeParameters;
        const variants = [...declaration.body.matchAll(/(?:^|\s)([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*([^)]*)\))?/g)];
        const typeArguments = typeParameters
            .map((parameter, index) => `\${${index + 1}:${parameter}}`)
            .join(", ");
        const qualifier = typeParameters.length === 0 ? name : `${name}<${typeArguments}>`;
        completions.push({
            label: name,
            kind: "Enum",
            detail: typeParameters.length === 0
                ? "Foundation enum"
                : `Foundation enum<${typeParameters.join(", ")}>`
        });
        for (const parameter of typeParameters) {
            completions.push({ label: parameter, kind: "TypeParameter", detail: `Type parameter of ${name}` });
        }
        for (const variant of variants) {
            completions.push({
                label: `${name}.${variant[1]}`,
                kind: "EnumMember",
                detail: `Variant of ${name}`,
                insertText: variant[2]
                    ? `${qualifier}.${variant[1]}(\${${typeParameters.length + 1}:value})`
                    : `${qualifier}.${variant[1]}`
            });
        }
    }

    for (const declaration of contracts) {
        const name = declaration.name;
        const typeParameters = declaration.typeParameters;
        completions.push({
            label: name,
            kind: "Contract",
            detail: typeParameters.length === 0
                ? "Foundation contract"
                : `Foundation contract<${typeParameters.join(", ")}>`
        });
        for (const parameter of typeParameters) {
            completions.push({
                label: parameter,
                kind: "TypeParameter",
                detail: `Type parameter of ${name}`
            });
        }
        for (const method of collectMethods(declaration.body, name, true)) {
            completions.push({
                label: method.name,
                kind: "Method",
                detail: `Contract method of ${name}`,
                insertText: `${method.name}(${method.parameters.map((parameter, index) =>
                    `\${${index + 1}:${parameter}}`).join(", ")})`
            });
        }
    }

    while ((match = functions.exec(masked)) !== null) {
        if (depths[match.index] !== 0) {
            continue;
        }
        const typeParameters = collectTypeParameters(match[2]);
        const parameters = collectParameters(match[3]);

        completions.push({
            label: match[1],
            kind: "Function",
            detail: "Foundation function",
            insertText: `${match[1]}(${parameters.map((name, index) => `\${${index + 1}:${name}}`).join(", ")})`
        });

        for (const parameter of typeParameters) {
            completions.push({
                label: parameter,
                kind: "TypeParameter",
                detail: `Type parameter of ${match[1]}`
            });
        }

        for (const parameter of parameters) {
            completions.push({
                label: parameter,
                kind: "Variable",
                detail: "Function parameter"
            });
        }
    }

    while ((match = bindings.exec(masked)) !== null) {
        if (/^\s*(?:\.[A-Za-z_][A-Za-z0-9_]*)*\s*\{/.test(
            masked.slice(bindings.lastIndex)
        )) {
            continue;
        }
        completions.push({
            label: match[1],
            kind: "Variable",
            detail: "Local binding"
        });
    }

    while ((match = structPatterns.exec(masked)) !== null) {
        for (const field of match[1].matchAll(
            /\b([A-Za-z_][A-Za-z0-9_]*)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?/g
        )) {
            completions.push({
                label: field[2] || field[1],
                kind: "Variable",
                detail: "Destructured field binding"
            });
        }
    }

    const unique = new Map();
    for (const completion of completions) {
        const key = `${completion.label}\0${completion.kind}`;
        if (!unique.has(key)) {
            unique.set(key, completion);
        }
    }

    return [...unique.values()].sort((left, right) => {
        if (left.label !== right.label) {
            return left.label < right.label ? -1 : 1;
        }
        if (left.kind === right.kind) {
            return 0;
        }
        return left.kind < right.kind ? -1 : 1;
    });
}

module.exports = {
    collectCompletions,
    collectPackageDeclarations,
    maskTrivia,
    splitTopLevel,
    staticCompletions
};
