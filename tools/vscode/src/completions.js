"use strict";

const staticCompletions = [
    { label: "package", kind: "Keyword" },
    { label: "import", kind: "Keyword" },
    { label: "as", kind: "Keyword" },
    { label: "struct", kind: "Keyword" },
    { label: "enum", kind: "Keyword" },
    { label: "fn", kind: "Keyword" },
    { label: "let", kind: "Keyword" },
    { label: "var", kind: "Keyword" },
    { label: "return", kind: "Keyword" },
    { label: "discard", kind: "Keyword" },
    { label: "if", kind: "Keyword" },
    { label: "else", kind: "Keyword" },
    { label: "while", kind: "Keyword" },
    { label: "match", kind: "Keyword" },
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

function collectStructFields(source) {
    const tokens = [...source.matchAll(/[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*|[<>,]/g)]
        .map((match) => match[0]);
    const fields = [];
    let offset = 0;
    while (offset + 1 < tokens.length) {
        fields.push(tokens[offset]);
        offset += 1;
        if (["own", "view", "edit"].includes(tokens[offset])) {
            offset += 1;
        }
        offset += 1;
        if (tokens[offset] !== "<") {
            continue;
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
    }
    return fields;
}

function collectPackageDeclarations(source) {
    const masked = maskTrivia(source);
    const packageMatch = masked.match(/\bpackage\s+([A-Za-z_][A-Za-z0-9_.]*)/);
    const imported = [...masked.matchAll(
        /\bimport\s+([A-Za-z_][A-Za-z0-9_.]*)(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?/g
    )].map((match) => ({
        packageName: match[1],
        alias: match[2] || match[1].split(".").at(-1)
    }));
    const functions = [...masked.matchAll(
        /\bfn\s+([A-Z][A-Za-z0-9_]*)(?:\s*<([^>{}]*)>)?\s*\(([^)]*)\)/g
    )].map((match) => ({
        name: match[1],
        kind: "Function",
        typeParameters: collectTypeParameters(match[2]),
        parameters: splitTopLevel(match[3], ",")
            .map((parameter) => parameter.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s+[A-Za-z_\[]/))
            .filter(Boolean)
            .map((parameter) => parameter[1])
    }));
    const structs = [...masked.matchAll(
        /\bstruct\s+([A-Z][A-Za-z0-9_]*)(?:\s*<([^>{}]*)>)?\s*\{/g
    )].map((match) => ({
        name: match[1],
        kind: "Struct",
        typeParameters: collectTypeParameters(match[2])
    }));
    const enums = [...masked.matchAll(
        /\benum\s+([A-Z][A-Za-z0-9_]*)(?:\s*<([^>{}]*)>)?\s*\{([^}]*)\}/g
    )].map((match) => ({
        name: match[1],
        kind: "Enum",
        typeParameters: collectTypeParameters(match[2]),
        variants: [...match[3].matchAll(
            /(?:^|\s)([A-Z][A-Za-z0-9_]*)(?:\s*\(\s*([^)]*)\))?/g
        )].map((variant) => ({ name: variant[1], payload: Boolean(variant[2]) }))
    }));

    return {
        packageName: packageMatch ? packageMatch[1] : null,
        imports: imported,
        declarations: [...functions, ...structs, ...enums]
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
    const completions = [...staticCompletions, ...importedCompletions(source, projectSources)];
    const functions = /\bfn\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*<([^>{}]*)>)?\s*\(([^)]*)\)/g;
    const structs = /\bstruct\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*<([^>{}]*)>)?\s*\{([^}]*)\}/g;
    const enums = /\benum\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*<([^>{}]*)>)?\s*\{([^}]*)\}/g;
    const bindings = /\b(?:let|var)\s+([A-Za-z_][A-Za-z0-9_]*)/g;
    let match;

    while ((match = structs.exec(masked)) !== null) {
        const name = match[1];
        const typeParameters = collectTypeParameters(match[2]);
        const fields = collectStructFields(match[3]);
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
    }

    while ((match = enums.exec(masked)) !== null) {
        const name = match[1];
        const typeParameters = collectTypeParameters(match[2]);
        const variants = [...match[3].matchAll(/(?:^|\s)([A-Za-z_][A-Za-z0-9_]*)(?:\s*\(\s*([^)]*)\))?/g)];
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

    while ((match = functions.exec(masked)) !== null) {
        const typeParameters = collectTypeParameters(match[2]);
        const parameters = splitTopLevel(match[3], ",")
            .map((parameter) => parameter.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s+[A-Za-z_\[]/))
            .filter(Boolean)
            .map((parameter) => parameter[1]);

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
        completions.push({
            label: match[1],
            kind: "Variable",
            detail: "Local binding"
        });
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
