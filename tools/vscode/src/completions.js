"use strict";

const staticCompletions = [
    { label: "fn", kind: "Keyword" },
    { label: "let", kind: "Keyword" },
    { label: "var", kind: "Keyword" },
    { label: "return", kind: "Keyword" },
    { label: "if", kind: "Keyword" },
    { label: "else", kind: "Keyword" },
    { label: "while", kind: "Keyword" },
    { label: "true", kind: "Constant" },
    { label: "false", kind: "Constant" },
    { label: "i32", kind: "TypeParameter" },
    { label: "bool", kind: "TypeParameter" },
    { label: "String", kind: "TypeParameter" },
    { label: "void", kind: "TypeParameter" },
    {
        label: "print",
        kind: "Function",
        detail: "builtin fn print(value: String) -> void",
        insertText: "print(${1:value});"
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

function collectCompletions(source) {
    const masked = maskTrivia(source);
    const completions = [...staticCompletions];
    const functions = /\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/g;
    const bindings = /\b(?:let|var)\s+([A-Za-z_][A-Za-z0-9_]*)/g;
    let match;

    while ((match = functions.exec(masked)) !== null) {
        const parameters = match[2]
            .split(",")
            .map((parameter) => parameter.match(/^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:/))
            .filter(Boolean)
            .map((parameter) => parameter[1]);

        completions.push({
            label: match[1],
            kind: "Function",
            detail: "Foundation function",
            insertText: `${match[1]}(${parameters.map((name, index) => `\${${index + 1}:${name}}`).join(", ")})`
        });

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

module.exports = { collectCompletions, maskTrivia, staticCompletions };
