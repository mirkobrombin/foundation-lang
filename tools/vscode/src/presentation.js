"use strict";

function documentationCommentOffsets(source) {
    const ranges = [];
    let offset = 0;
    while (offset < source.length) {
        if (source[offset] === '"') {
            ++offset;
            while (offset < source.length) {
                if (source[offset] === "\\") {
                    offset += Math.min(2, source.length - offset);
                } else if (source[offset++] === '"') {
                    break;
                }
            }
            continue;
        }
        if (source.startsWith("//", offset)) {
            const start = offset;
            const end = source.indexOf("\n", offset + 2);
            offset = end < 0 ? source.length : end;
            if (source.startsWith("///", start)) {
                ranges.push({ start, end: offset });
            }
            continue;
        }
        if (source.startsWith("/*", offset)) {
            const start = offset;
            const documentation = source.startsWith("/**", offset);
            let depth = 1;
            offset += 2;
            while (offset < source.length && depth > 0) {
                if (source.startsWith("/*", offset)) {
                    ++depth;
                    offset += 2;
                } else if (source.startsWith("*/", offset)) {
                    --depth;
                    offset += 2;
                } else {
                    ++offset;
                }
            }
            if (documentation) {
                ranges.push({ start, end: offset });
            }
            continue;
        }
        ++offset;
    }
    return ranges;
}

function maskedTrivia(source) {
    const result = [...source];
    const mask = (start, end) => {
        for (let offset = start; offset < end; ++offset) {
            if (result[offset] !== "\n" && result[offset] !== "\r") {
                result[offset] = " ";
            }
        }
    };
    let offset = 0;
    while (offset < source.length) {
        if (source[offset] === '"') {
            const start = offset++;
            while (offset < source.length) {
                if (source[offset] === "\\") {
                    offset += Math.min(2, source.length - offset);
                } else if (source[offset++] === '"') {
                    break;
                }
            }
            mask(start, offset);
            continue;
        }
        if (source.startsWith("//", offset)) {
            const start = offset;
            const end = source.indexOf("\n", offset + 2);
            offset = end < 0 ? source.length : end;
            mask(start, offset);
            continue;
        }
        if (source.startsWith("/*", offset)) {
            const start = offset;
            let depth = 1;
            offset += 2;
            while (offset < source.length && depth > 0) {
                if (source.startsWith("/*", offset)) {
                    ++depth;
                    offset += 2;
                } else if (source.startsWith("*/", offset)) {
                    --depth;
                    offset += 2;
                } else {
                    ++offset;
                }
            }
            mask(start, offset);
            continue;
        }
        ++offset;
    }
    return result.join("");
}

function compositeTypeDeclarations(source) {
    const masked = maskedTrivia(source);
    const declarations = [];
    const pattern = /^\s*(struct|methods)\s+([A-Za-z_][A-Za-z0-9_]*)\b/gm;
    for (const match of masked.matchAll(pattern)) {
        const nameOffset = match.index + match[0].lastIndexOf(match[2]);
        declarations.push({
            kind: match[1],
            name: match[2],
            start: nameOffset,
            end: nameOffset + match[2].length
        });
    }
    return declarations;
}

module.exports = {
    compositeTypeDeclarations,
    documentationCommentOffsets,
    maskedTrivia
};
