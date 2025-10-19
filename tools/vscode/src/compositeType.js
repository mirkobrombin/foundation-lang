"use strict";

function marker(id, boundary) {
    return `// foundation:fragment ${id} ${boundary}`;
}

function markerCount(text, value) {
    let count = 0;
    let offset = 0;
    while ((offset = text.indexOf(value, offset)) >= 0) {
        ++count;
        offset += value.length;
    }
    return count;
}

function renderCompositeType(model) {
    const lines = [
        `// Foundation Composite Type View: ${model.typeName}`,
        "// Save this file to apply all source edits atomically.",
        "// Source markers are required and are checked for conflicts.",
        "",
        `package ${model.packageName}`
    ];
    for (const imported of model.imports || []) {
        lines.push(imported);
    }
    lines.push("");

    const fragments = model.fragments.map((fragment, id) => {
        const indent = fragment.kind === "method" || fragment.key === "struct-suffix"
            ? "    "
            : "";
        const begin = marker(id, "begin");
        const end = marker(id, "end");
        lines.push(`${indent}// Source: ${fragment.displayPath || fragment.path}:${fragment.line}`);
        lines.push(`${indent}${begin}`);
        const separatorAdded = !fragment.text.endsWith("\n");
        lines.push(fragment.text + (separatorAdded ? "\n" : "") + `${indent}${end}`);
        lines.push("");
        return { ...fragment, id, begin, end, separatorAdded };
    });
    return { text: lines.join("\n"), fragments };
}

function extractCompositeEdits(text, fragments) {
    const edits = [];
    for (const fragment of fragments) {
        if (markerCount(text, fragment.begin) !== 1 ||
            markerCount(text, fragment.end) !== 1) {
            throw new Error(`Source markers for ${fragment.displayPath || fragment.path} changed`);
        }
        const begin = text.indexOf(fragment.begin);
        const end = text.indexOf(fragment.end);
        if (end <= begin) {
            throw new Error(`Source markers for ${fragment.displayPath || fragment.path} are out of order`);
        }
        const contentStart = text.indexOf("\n", begin + fragment.begin.length);
        if (contentStart < 0 || contentStart >= end) {
            throw new Error(`Source fragment for ${fragment.displayPath || fragment.path} is malformed`);
        }
        let value = text.slice(contentStart + 1, end);
        const markerLineStart = value.lastIndexOf("\n");
        if (markerLineStart >= 0 && value.slice(markerLineStart + 1).trim() === "") {
            value = value.slice(0, markerLineStart + 1);
        }
        if (fragment.separatorAdded) {
            if (!value.endsWith("\n")) {
                throw new Error(`Source fragment for ${fragment.displayPath || fragment.path} lost its boundary`);
            }
            value = value.slice(0, -1);
        }
        edits.push({ fragment, text: value });
    }
    return edits;
}

function offsetAt(text, position) {
    let line = 0;
    let offset = 0;
    while (line < position.line && offset < text.length) {
        const newline = text.indexOf("\n", offset);
        if (newline < 0) {
            return text.length;
        }
        offset = newline + 1;
        ++line;
    }
    return Math.min(text.length, offset + position.character);
}

function textAtRange(text, range) {
    return text.slice(offsetAt(text, range.start), offsetAt(text, range.end));
}

function compositeTypeName(text, fallback) {
    return text.match(/\bstruct\s+([A-Za-z_][A-Za-z0-9_]*)/)?.[1] || fallback;
}

module.exports = {
    compositeTypeName,
    extractCompositeEdits,
    offsetAt,
    renderCompositeType,
    textAtRange
};
