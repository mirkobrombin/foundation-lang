"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const {
    collectCompletions,
    findHover,
    maskAttributeApplications,
    maskTrivia,
    staticCompletions
} = require("../src/completions");
const {
    FoundationLanguageClient,
    MessageReader,
    encodeMessage
} = require("../src/languageClient");

const extensionRoot = path.resolve(__dirname, "..");
const repositoryRoot = path.resolve(extensionRoot, "../..");

function readJson(relativePath) {
    return JSON.parse(fs.readFileSync(path.join(extensionRoot, relativePath), "utf8"));
}

test("registers Foundation source files", () => {
    const manifest = readJson("package.json");
    const language = manifest.contributes.languages[0];
    const grammar = manifest.contributes.grammars[0];

    assert.equal(language.id, "foundation");
    assert.deepEqual(language.extensions, [".fdn"]);
    assert.equal(grammar.language, "foundation");
    assert.equal(grammar.scopeName, "source.foundation");
    assert.ok(fs.existsSync(path.join(extensionRoot, grammar.path)));
    assert.ok(fs.existsSync(path.join(extensionRoot, language.configuration)));

    const vsixManifest = fs.readFileSync(
        path.join(extensionRoot, "vsix/extension.vsixmanifest"),
        "utf8"
    );
    assert.match(vsixManifest, new RegExp(`Version="${manifest.version}"`));

    const packagingScript = fs.readFileSync(
        path.join(extensionRoot, "scripts/package.sh"),
        "utf8"
    );
    const languageClient = fs.readFileSync(
        path.join(extensionRoot, "src/languageClient.js"),
        "utf8"
    );
    assert.match(packagingScript, /package\.json/);
    assert.match(packagingScript, /foundation-lang-\$version\.vsix/);
    assert.match(packagingScript, /languageClient\.js/);
    assert.match(languageClient, /registerDocumentSymbolProvider/);
    assert.match(languageClient, /registerWorkspaceSymbolProvider/);
    assert.match(languageClient, /registerCompletionItemProvider/);
    assert.match(languageClient, /registerSignatureHelpProvider/);
    assert.match(languageClient, /registerHoverProvider/);
    assert.match(languageClient, /registerDeclarationProvider/);
    assert.match(languageClient, /textDocument\/declaration/);
    assert.match(languageClient, /registerDefinitionProvider/);
    assert.match(languageClient, /registerTypeDefinitionProvider/);
    assert.match(languageClient, /textDocument\/typeDefinition/);
    assert.match(languageClient, /registerImplementationProvider/);
    assert.match(languageClient, /registerDocumentHighlightProvider/);
    assert.match(languageClient, /registerCodeLensProvider/);
    assert.match(languageClient, /textDocument\/codeLens/);
    assert.match(languageClient, /registerTypeHierarchyProvider/);
    assert.match(languageClient, /typeHierarchy\/supertypes/);
    assert.match(languageClient, /typeHierarchy\/subtypes/);
    assert.match(languageClient, /registerCallHierarchyProvider/);
    assert.match(languageClient, /callHierarchy\/incomingCalls/);
    assert.match(languageClient, /callHierarchy\/outgoingCalls/);
    assert.match(languageClient, /registerReferenceProvider/);
    assert.match(languageClient, /registerRenameProvider/);
    assert.match(languageClient, /registerDocumentSemanticTokensProvider/);
    assert.match(languageClient, /registerInlayHintsProvider/);
    assert.match(languageClient, /registerCodeActionsProvider/);
    assert.match(languageClient, /textDocument\/codeAction/);
    assert.match(languageClient, /registerFoldingRangeProvider/);
    assert.match(languageClient, /textDocument\/foldingRange/);
    assert.match(languageClient, /registerSelectionRangeProvider/);
    assert.match(languageClient, /textDocument\/selectionRange/);
    assert.match(languageClient, /registerDocumentFormattingEditProvider/);
    assert.match(languageClient, /registerDocumentRangeFormattingEditProvider/);
    assert.match(languageClient, /textDocument\/rangeFormatting/);
    assert.match(languageClient, /createFileSystemWatcher\("\*\*\/\*\.fdn"\)/);
    assert.match(languageClient, /createFileSystemWatcher\(\s*"\*\*\/foundation\.package"/);
    assert.match(languageClient, /createFileSystemWatcher\("\*\*\/foundation\.lock"\)/);
    assert.match(languageClient, /workspace\/didChangeWatchedFiles/);
    assert.equal(manifest.version, "0.27.0");
    assert.equal(
        manifest.contributes.configuration.properties["foundation.languageServer.path"].default,
        ""
    );
});

test("frames language server messages across stream chunks", () => {
    const messages = [];
    const errors = [];
    const reader = new MessageReader(
        (message) => messages.push(message),
        (error) => errors.push(error)
    );
    const first = encodeMessage({ jsonrpc: "2.0", id: 1, result: { ready: true } });
    const second = encodeMessage({
        jsonrpc: "2.0",
        method: "textDocument/publishDiagnostics",
        params: { uri: "file:///tmp/main.fdn", diagnostics: [] }
    });
    const stream = Buffer.concat([first, second]);

    reader.append(stream.subarray(0, 7));
    reader.append(stream.subarray(7, first.length + 11));
    reader.append(stream.subarray(first.length + 11));

    assert.equal(errors.length, 0);
    assert.equal(messages.length, 2);
    assert.deepEqual(messages[0].result, { ready: true });
    assert.equal(messages[1].method, "textDocument/publishDiagnostics");
});

test("cancels pending language server requests", async () => {
    class CancellationError extends Error {}
    const sent = [];
    const token = {
        isCancellationRequested: false,
        onCancellationRequested(listener) {
            this.listener = listener;
            return { dispose() { token.disposed = true; } };
        }
    };
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = { CancellationError };
    client.nextId = 1;
    client.pending = new Map();
    client.send = (message) => sent.push(message);

    const request = client.request("workspace/symbol", { query: "Value" }, token);
    token.listener();

    await assert.rejects(request, CancellationError);
    assert.deepEqual(sent, [
        { jsonrpc: "2.0", id: 1, method: "workspace/symbol", params: { query: "Value" } },
        { jsonrpc: "2.0", method: "$/cancelRequest", params: { id: 1 } }
    ]);
    assert.equal(client.pending.size, 0);
    assert.equal(token.disposed, true);
});

test("maps all package definition locations", async () => {
    class Position {
        constructor(line, character) {
            Object.assign(this, { line, character });
        }
    }
    class Range {
        constructor(startLine, startCharacter, endLine, endCharacter) {
            this.start = new Position(startLine, startCharacter);
            this.end = new Position(endLine, endCharacter);
        }
    }
    class Location {
        constructor(uri, range) {
            Object.assign(this, { uri, range });
        }
    }
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = {
        Location,
        Range,
        Uri: { parse: (value) => ({ value }) }
    };
    client.request = async () => [
        {
            uri: "file:///tmp/first.fdn",
            range: {
                start: { line: 0, character: 8 },
                end: { line: 0, character: 22 }
            }
        },
        {
            uri: "file:///tmp/second.fdn",
            range: {
                start: { line: 0, character: 8 },
                end: { line: 0, character: 22 }
            }
        }
    ];

    const locations = await client.definition(
        { uri: { toString: () => "file:///tmp/main.fdn" } },
        new Position(1, 12)
    );

    assert.equal(locations.length, 2);
    assert.equal(locations[0].uri.value, "file:///tmp/first.fdn");
    assert.equal(locations[1].uri.value, "file:///tmp/second.fdn");
    assert.equal(locations[1].range.start.character, 8);
});

test("maps structural folding and selection ranges", async () => {
    class Position {
        constructor(line, character) {
            Object.assign(this, { line, character });
        }
    }
    class Range {
        constructor(startLine, startCharacter, endLine, endCharacter) {
            this.start = new Position(startLine, startCharacter);
            this.end = new Position(endLine, endCharacter);
        }
    }
    class FoldingRange {
        constructor(start, end, kind) {
            Object.assign(this, { start, end, kind });
        }
    }
    class SelectionRange {
        constructor(range, parent) {
            Object.assign(this, { range, parent });
        }
    }
    const requests = [];
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = {
        FoldingRange,
        FoldingRangeKind: { Imports: "imports" },
        Position,
        Range,
        SelectionRange
    };
    client.request = async (method, params) => {
        requests.push({ method, params });
        if (method === "textDocument/foldingRange") {
            return [
                { startLine: 1, endLine: 2, kind: "imports" },
                { startLine: 5, endLine: 9 }
            ];
        }
        return [{
            range: {
                start: { line: 7, character: 15 },
                end: { line: 7, character: 20 }
            },
            parent: {
                range: {
                    start: { line: 6, character: 12 },
                    end: { line: 8, character: 5 }
                }
            }
        }];
    };
    const document = { uri: { toString: () => "file:///tmp/main.fdn" } };

    const folds = await client.foldingRanges(document);
    const selections = await client.selectionRanges(document, [new Position(7, 17)]);

    assert.equal(requests[0].method, "textDocument/foldingRange");
    assert.equal(folds[0].kind, "imports");
    assert.equal(folds[1].start, 5);
    assert.equal(requests[1].method, "textDocument/selectionRange");
    assert.deepEqual(requests[1].params.positions, [{ line: 7, character: 17 }]);
    assert.equal(selections[0].range.start.character, 15);
    assert.equal(selections[0].parent.range.start.line, 6);
});

test("maps language server formatting edits", async () => {
    class Position {
        constructor(line, character) {
            this.line = line;
            this.character = character;
        }
    }
    class Range {
        constructor(startLine, startCharacter, endLine, endCharacter) {
            this.start = new Position(startLine, startCharacter);
            this.end = new Position(endLine, endCharacter);
        }
    }
    class TextEdit {
        constructor(range, newText) {
            this.range = range;
            this.newText = newText;
        }
    }
    const requests = [];
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = { Position, Range, TextEdit };
    client.request = async (method, params) => {
        requests.push({ method, params });
        return [{
            range: {
                start: { line: 1, character: 0 },
                end: { line: 1, character: 6 }
            },
            newText: "    value"
        }];
    };
    const document = { uri: { toString: () => "file:///tmp/main.fdn" } };
    const range = new Range(1, 0, 2, 0);
    const edits = await client.rangeFormatting(document, range, { tabSize: 4 }, null);

    assert.equal(requests[0].method, "textDocument/rangeFormatting");
    assert.deepEqual(requests[0].params.range, {
        start: { line: 1, character: 0 },
        end: { line: 2, character: 0 }
    });
    assert.equal(edits[0].newText, "    value");
    assert.equal(edits[0].range.start.line, 1);
});

test("maps compiler quick fixes to workspace edits", async () => {
    class Position {
        constructor(line, character) {
            this.line = line;
            this.character = character;
        }
    }
    class Range {
        constructor(startLine, startCharacter, endLine, endCharacter) {
            this.start = new Position(startLine, startCharacter);
            this.end = new Position(endLine, endCharacter);
        }
    }
    class CodeAction {
        constructor(title, kind) {
            this.title = title;
            this.kind = kind;
        }
    }
    class WorkspaceEdit {
        constructor() {
            this.replacements = [];
        }

        replace(uri, range, newText) {
            this.replacements.push({ uri, range, newText });
        }
    }
    const requests = [];
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = {
        Position,
        Range,
        CodeAction,
        WorkspaceEdit,
        CodeActionKind: { QuickFix: "quickfix" },
        Uri: { parse: (value) => ({ value }) }
    };
    client.request = async (method, params) => {
        requests.push({ method, params });
        return [{
            title: "Handle explicitly with discard",
            kind: "quickfix",
            isPreferred: true,
            edit: {
                changes: {
                    "file:///tmp/main.fdn": [{
                        range: {
                            start: { line: 4, character: 4 },
                            end: { line: 4, character: 4 }
                        },
                        newText: "discard "
                    }]
                }
            }
        }];
    };
    const document = { uri: { toString: () => "file:///tmp/main.fdn" } };
    const range = new Range(4, 8, 4, 8);
    const diagnostic = {
        range: new Range(4, 4, 4, 16),
        severity: 0,
        code: "FDN2051",
        source: "foundation",
        message: "Result value must be handled or discarded"
    };

    const actions = await client.codeActions(document, range, { diagnostics: [diagnostic] });

    assert.equal(requests[0].method, "textDocument/codeAction");
    assert.equal(requests[0].params.context.diagnostics[0].code, "FDN2051");
    assert.equal(actions[0].title, "Handle explicitly with discard");
    assert.equal(actions[0].isPreferred, true);
    assert.equal(actions[0].edit.replacements[0].newText, "discard ");
    assert.equal(actions[0].edit.replacements[0].range.start.character, 4);
});

test("maps reference code lenses to clickable VS Code locations", async () => {
    class Position {
        constructor(line, character) {
            this.line = line;
            this.character = character;
        }
    }
    class Range {
        constructor(startLine, startCharacter, endLine, endCharacter) {
            this.start = new Position(startLine, startCharacter);
            this.end = new Position(endLine, endCharacter);
        }
    }
    class Location {
        constructor(uri, range) {
            this.uri = uri;
            this.range = range;
        }
    }
    class CodeLens {
        constructor(range, command) {
            this.range = range;
            this.command = command;
        }
    }
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = {
        Position,
        Range,
        Location,
        CodeLens,
        Uri: { parse: (value) => ({ value }) }
    };
    client.request = async () => [{
        range: {
            start: { line: 1, character: 3 },
            end: { line: 1, character: 6 }
        },
        command: {
            title: "1 reference",
            command: "editor.action.showReferences",
            arguments: [
                "file:///tmp/main.fdn",
                { line: 1, character: 3 },
                [{
                    uri: "file:///tmp/main.fdn",
                    range: {
                        start: { line: 2, character: 12 },
                        end: { line: 2, character: 15 }
                    }
                }]
            ]
        }
    }];

    const lenses = await client.codeLenses({
        uri: { toString: () => "file:///tmp/main.fdn" }
    });

    assert.equal(lenses.length, 1);
    assert.equal(lenses[0].command.title, "1 reference");
    assert.equal(lenses[0].command.arguments[0].value, "file:///tmp/main.fdn");
    assert.deepEqual(lenses[0].command.arguments[1], new Position(1, 3));
    assert.equal(lenses[0].command.arguments[2][0].range.start.line, 2);
});

test("round trips compiler type hierarchy identities", async () => {
    class Position {
        constructor(line, character) {
            this.line = line;
            this.character = character;
        }
    }
    class Range {
        constructor(startLine, startCharacter, endLine, endCharacter) {
            this.start = new Position(startLine, startCharacter);
            this.end = new Position(endLine, endCharacter);
        }
    }
    class TypeHierarchyItem {
        constructor(kind, name, detail, uri, range, selectionRange) {
            Object.assign(this, { kind, name, detail, uri, range, selectionRange });
        }
    }
    const value = {
        name: "Named",
        kind: 11,
        detail: "contract Named",
        uri: "file:///tmp/main.fdn",
        range: {
            start: { line: 1, character: 9 },
            end: { line: 1, character: 14 }
        },
        selectionRange: {
            start: { line: 1, character: 9 },
            end: { line: 1, character: 14 }
        },
        data: {
            kind: "contract",
            name: "Named",
            scope: "type:sample",
            uri: "file:///tmp/main.fdn"
        }
    };
    const requests = [];
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = {
        Position,
        Range,
        TypeHierarchyItem,
        Uri: { parse: (uri) => ({ value: uri }) }
    };
    client.request = async (method, params) => {
        requests.push({ method, params });
        return [value];
    };
    const document = { uri: { toString: () => value.uri } };

    const prepared = await client.prepareTypeHierarchy(document, new Position(1, 10));
    const parents = await client.typeHierarchySupertypes(prepared[0]);
    const children = await client.typeHierarchySubtypes(prepared[0]);

    assert.equal(prepared[0].kind, 10);
    assert.deepEqual(prepared[0].data, value.data);
    assert.equal(parents[0].name, "Named");
    assert.equal(children[0].uri.value, value.uri);
    assert.deepEqual(requests.map((request) => request.method), [
        "textDocument/prepareTypeHierarchy",
        "typeHierarchy/supertypes",
        "typeHierarchy/subtypes"
    ]);
    assert.deepEqual(requests[1].params.item.data, value.data);
});

test("maps compiler call hierarchy edges to VS Code calls", async () => {
    class Position {
        constructor(line, character) {
            this.line = line;
            this.character = character;
        }
    }
    class Range {
        constructor(startLine, startCharacter, endLine, endCharacter) {
            this.start = new Position(startLine, startCharacter);
            this.end = new Position(endLine, endCharacter);
        }
    }
    class CallHierarchyItem {
        constructor(kind, name, detail, uri, range, selectionRange) {
            Object.assign(this, { kind, name, detail, uri, range, selectionRange });
        }
    }
    class CallHierarchyIncomingCall {
        constructor(from, fromRanges) {
            Object.assign(this, { from, fromRanges });
        }
    }
    class CallHierarchyOutgoingCall {
        constructor(to, fromRanges) {
            Object.assign(this, { to, fromRanges });
        }
    }
    const range = {
        start: { line: 1, character: 3 },
        end: { line: 1, character: 6 }
    };
    const item = {
        name: "add",
        kind: 12,
        detail: "fn add() i32",
        uri: "file:///tmp/main.fdn",
        range,
        selectionRange: range,
        data: {
            kind: "function",
            name: "add",
            scope: "function:sample",
            uri: "file:///tmp/main.fdn"
        }
    };
    const methods = [];
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = {
        Position,
        Range,
        CallHierarchyItem,
        CallHierarchyIncomingCall,
        CallHierarchyOutgoingCall,
        Uri: { parse: (uri) => ({ value: uri }) }
    };
    client.request = async (method) => {
        methods.push(method);
        if (method === "callHierarchy/incomingCalls") {
            return [{ from: item, fromRanges: [range] }];
        }
        if (method === "callHierarchy/outgoingCalls") {
            return [{ to: item, fromRanges: [range] }];
        }
        return [item];
    };
    const document = { uri: { toString: () => item.uri } };

    const prepared = await client.prepareCallHierarchy(document, new Position(1, 4));
    const incoming = await client.callHierarchyIncomingCalls(prepared[0]);
    const outgoing = await client.callHierarchyOutgoingCalls(prepared[0]);

    assert.equal(prepared[0].name, "add");
    assert.deepEqual(prepared[0].data, item.data);
    assert.equal(incoming[0].from.name, "add");
    assert.equal(incoming[0].fromRanges[0].start.line, 1);
    assert.equal(outgoing[0].to.uri.value, item.uri);
    assert.deepEqual(methods, [
        "textDocument/prepareCallHierarchy",
        "callHierarchy/incomingCalls",
        "callHierarchy/outgoingCalls"
    ]);
});

test("grammar and completions track compiler keywords", () => {
    const lexer = fs.readFileSync(path.join(repositoryRoot, "stage0/src/lexer.cpp"), "utf8");
    const grammar = fs.readFileSync(
        path.join(extensionRoot, "syntaxes/foundation.tmLanguage.json"),
        "utf8"
    );
    const compilerKeywords = [...lexer.matchAll(/text == "([A-Za-z_][A-Za-z0-9_]*)"/g)]
        .map((match) => match[1]);
    const completionLabels = new Set(staticCompletions.map((entry) => entry.label));

    assert.deepEqual(compilerKeywords, [
        "package",
        "import",
        "as",
        "extern",
        "struct",
        "enum",
        "contract",
        "attribute",
        "implements",
        "extends",
        "by",
        "fn",
        "let",
        "const",
        "var",
        "return",
        "discard",
        "if",
        "else",
        "while",
        "match",
        "capture",
        "replace",
        "with",
        "own",
        "view",
        "edit",
        "true",
        "false"
    ]);
    for (const keyword of compilerKeywords) {
        assert.match(grammar, new RegExp(`\\b${keyword}\\b`));
        assert.ok(completionLabels.has(keyword));
    }

    for (const type of ["i32", "u64", "bool", "String", "void", "Option", "Result", "len", "print", "panic"]) {
        assert.match(grammar, new RegExp(`\\b${type}\\b`));
        assert.ok(completionLabels.has(type));
    }
    for (const keyword of [
        "const", "methods", "forward", "service", "action", "state_machine", "pipeline",
        "saga", "task", "spawn", "select", "test", "unsafe", "new", "for", "in"
    ]) {
        assert.match(grammar, new RegExp(`\\b${keyword}\\b`));
        assert.ok(completionLabels.has(keyword));
    }
    for (const type of [
        "i8", "i16", "i64", "u8", "u16", "u32", "f32", "f64", "isize", "usize",
        "never", "UUID"
    ]) {
        assert.match(grammar, new RegExp(`\\b${type}\\b`));
        assert.ok(completionLabels.has(type));
    }
    assert.ok(completionLabels.has("c"));
    assert.ok(completionLabels.has("@target(...)"));
    assert.ok(completionLabels.has("targets(...)"));
    assert.ok(completionLabels.has("repeatable"));
    for (const standard of [
        "std.platform", "platform.Current", "platform.Name",
        "std.env", "env.Get", "env.Home",
        "std.text", "text.ByteLen", "text.Contains", "text.NewBuilder",
        "std.path", "path.Join",
        "std.parse", "parse.U64",
        "std.fs", "fs.OpenLines", "fs.OpenDir", "fs.Size", "fs.Modified",
        "fs.LineReader.Next", "fs.LineReader.NextLimited",
        "std.format", "format.I32", "format.U64",
        "std.json", "json.Parse",
        "std.time", "time.Now", "time.FromUnix", "time.Instant.FormatUtc"
    ]) {
        assert.ok(completionLabels.has(standard));
    }
    const parsedGrammar = JSON.parse(grammar);
    const targetAttribute = parsedGrammar.repository.compilerAttributes.patterns[0];
    assert.match(targetAttribute.match, /target/);
    assert.match(targetAttribute.captures[4].name, /target/);
    assert.match(parsedGrammar.repository.attributeDefinitions.patterns[0].begin, /attribute/);
    assert.match(parsedGrammar.repository.attributeApplications.patterns[0].begin, /@/);
    const cAbiDeclaration = parsedGrammar.repository.cAbiDeclarations.patterns[0];
    assert.equal(cAbiDeclaration.name, "meta.function.external.foundation");
    assert.match(cAbiDeclaration.begin, /extern/);
    assert.ok(cAbiDeclaration.patterns.some((pattern) =>
        pattern.captures?.[2]?.name === "entity.name.function.external.foundation"
    ));
    for (const sequence of ["[N]T", "[T]", "&[T]", "view [T]", "edit [T]"]) {
        assert.ok(completionLabels.has(sequence));
    }
    assert.ok(completionLabels.has("fn(...) R"));
    const snippets = readJson("snippets/foundation.json");
    assert.match(snippets["Target declaration"].body.join("\n"), /@target/);
    assert.match(snippets["Typed attribute declaration"].body, /targets/);
    assert.match(snippets["Typed attribute application"].body, /@/);
    assert.match(snippets["Read environment value"].body.join("\n"), /env\.Get\(/);
    assert.doesNotMatch(snippets["Read environment value"].body.join("\n"), /\blet\b|\bview\b/);
    assert.match(snippets["Join path"].body, /path\.Join\(/);
    assert.match(snippets["Open line reader"].body.join("\n"), /fs\.OpenLines\(/);
    assert.match(snippets["Parse JSON value"].body.join("\n"), /json\.Parse\(/);
    assert.match(snippets["String builder"].body.join("\n"), /text\.NewBuilder/);
    assert.match(snippets["Format UTC time"].body.join("\n"), /FormatUtc/);
    assert.match(snippets["Main function with arguments"].body.join("\n"),
        /main\(args \[String\]\) i32/);
    assert.equal(parsedGrammar.repository.comments.patterns[0].name,
        "comment.line.documentation.foundation");
    assert.ok(parsedGrammar.repository.comments.patterns.some((pattern) =>
        pattern.name === "comment.block.documentation.foundation"
    ));
    const blockPatterns = parsedGrammar.repository.comments.patterns.filter((pattern) =>
        pattern.name?.startsWith("comment.block.")
    );
    assert.ok(blockPatterns.every((pattern) =>
        pattern.patterns?.[0]?.include === "#nestedBlockComments"
    ));
    assert.ok(parsedGrammar.repository.nestedBlockComments.patterns.every((pattern) =>
        pattern.name.startsWith("comment.block.")
    ));
    assert.match(grammar, /\\\\\[0nrt/);
    assert.equal(
        parsedGrammar.repository.punctuation.patterns[1].match,
        "[(){}\\[\\]]"
    );
});

test("provides hover inventory for standard and project symbols", () => {
    const source = `
        fn localValue(input i32) i32 { input }
        fn main() i32 { localValue(1) }
    `;
    assert.equal(findHover(source, "Now").detail, "fn Now() time.Instant");
    assert.equal(
        findHover(source, "NextLimited").detail,
        "fn NextLimited(edit, limit u64) Result<Option<String>, fs.Error>"
    );
    assert.equal(findHover(source, "localValue").detail, "Foundation function");
    assert.equal(findHover(source, "unknown"), undefined);
});

test("collects typed attributes without polluting declaration fields", () => {
    const application = `
        package example.app
        import example.metadata as meta

        attribute Local(value String) targets(struct, field) repeatable

        @Local("record")
        struct Request {
            @meta.Field(name = "user_id")
            userId u64
        }

        @meta.Route(.GET, "/users/:id")
        fn main() i32 { 0 }
    `;
    const library = `
        package example.metadata
        enum Method { GET }
        attribute Route(method Method, path String) targets(fn)
        attribute Field(name String) targets(field)
        attribute hidden() targets(fn)
    `;
    const completions = collectCompletions(application, [application, library]);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("@Local").kind, "Attribute");
    assert.equal(byLabel.get("@Local").insertText, "@Local(${1:value})");
    assert.equal(byLabel.get("@meta.Route").insertText,
        "@meta.Route(${1:method}, ${2:path})");
    assert.equal(byLabel.get("@meta.Field").kind, "Attribute");
    assert.equal(byLabel.has("@meta.hidden"), false);
    assert.equal(byLabel.get("userId").detail, "Field of Request");
    assert.equal(byLabel.has("Field"), false);
    assert.match(findHover(application, "Local", [application, library]).detail, /repeatable/);
    assert.match(findHover(application, "Route", [application, library]).detail,
        /example\.metadata/);
});

test("tracks function values and explicit closure captures", () => {
    const grammar = readJson("syntaxes/foundation.tmLanguage.json");
    const snippets = readJson("snippets/foundation.json");
    const completions = collectCompletions(`
        struct Callback { call fn(i32) i32 }
        fn apply(value i32, operation view fn(i32) i32) i32 {
            operation(value)
        }
        fn main() i32 {
            let factor = 2
            let scale fn(i32) i32 = fn(value i32) i32 capture factor {
                value * factor
            }
            scale(21) - 42
        }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("capture").kind, "Keyword");
    assert.equal(byLabel.get("Callback").insertText, "Callback { call = ${1:call} }");
    assert.match(grammar.repository.functionTypes.patterns[0].begin, /fn/);
    assert.match(grammar.repository.captureClauses.patterns[0].begin, /capture/);
    assert.match(grammar.repository.captureClauses.patterns[0].beginCaptures[1].name, /keyword/);
    assert.match(grammar.repository.captureClauses.patterns[0].patterns[1].name, /capture/);
    assert.equal(snippets["Function value type"].prefix, "fntype");
    assert.equal(snippets.Closure.prefix, "closure");
    assert.equal(snippets["Owning closure"].prefix, "closureown");
});

test("collects enums and dot-qualified variants", () => {
    const completions = collectCompletions(`
        enum Outcome {
            Empty
            Value(i32)
        }
        fn main() i32 { 0 }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("Outcome").kind, "Enum");
    assert.equal(byLabel.get("Outcome.Empty").insertText, "Outcome.Empty");
    assert.equal(byLabel.get("Outcome.Value").insertText, "Outcome.Value(${1:value})");
});

test("collects packages and exported project declarations", () => {
    const application = `
        package example.app
        import example.math as math
        fn main() i32 { math.Add(1, 2) }
    `;
    const library = `
        package example.math
        struct Box<T> { Value T hidden i32 }
        enum Status<T> { Ready Value(T) hidden }
        fn Add(left i32, right i32) i32 { left + right }
        fn hidden() i32 { 0 }
    `;
    const completions = collectCompletions(application, [application, library]);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("example.math").kind, "Module");
    assert.equal(byLabel.get("math").detail, "Alias for example.math");
    assert.equal(byLabel.get("math.Add").insertText, "math.Add(${1:left}, ${2:right})");
    assert.equal(byLabel.get("math.Box").insertText, "math.Box<${1:T}>");
    assert.equal(byLabel.get("math.Status.Ready").kind, "EnumMember");
    assert.equal(
        byLabel.get("math.Status.Value").insertText,
        "math.Status<${1:T}>.Value(${2:value})"
    );
    assert.equal(byLabel.has("math.hidden"), false);

    const grammar = readJson("syntaxes/foundation.tmLanguage.json");
    assert.match(
        grammar.repository.packageDeclarations.patterns[0].captures[2].name,
        /namespace/
    );
    const packageDeclaration = new RegExp(
        grammar.repository.packageDeclarations.patterns[0].match
    );
    assert.equal(
        "package example.app as alias".match(packageDeclaration)[0],
        "package example.app"
    );
    const importDeclaration = new RegExp(
        grammar.repository.packageDeclarations.patterns[1].match
    );
    assert.match("import example.math as math", importDeclaration);
    assert.match(
        grammar.repository.qualifiedFunctionCalls.patterns[0].captures[3].name,
        /function\.call/
    );
    const qualifiedType = new RegExp(grammar.repository.qualifiedTypes.patterns[0].match);
    assert.match("math.Box<i32> {", qualifiedType);
    assert.doesNotMatch("value.Visible + 1", qualifiedType);
    const typedVariant = new RegExp(grammar.repository.enumConstructors.patterns[0].match);
    assert.match("Status.Ready", typedVariant);
    assert.doesNotMatch("value.Visible", typedVariant);
});

test("collects generic declarations and type parameters", () => {
    const completions = collectCompletions(`
        struct Pair<T, U> { first T second U }
        enum Outcome<T, E> { Ok(T) Err(E) }
        fn keep<T>(value Outcome<T, bool>, fallback T) T { fallback }
        fn main() i32 { 0 }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("Pair").detail, "Foundation struct<T, U>");
    assert.equal(byLabel.get("Outcome").detail, "Foundation enum<T, E>");
    assert.equal(
        byLabel.get("Outcome.Ok").insertText,
        "Outcome<${1:T}, ${2:E}>.Ok(${3:value})"
    );
    assert.equal(byLabel.get("T").kind, "TypeParameter");
    assert.equal(byLabel.get("E").kind, "TypeParameter");
    assert.equal(byLabel.get("fallback").detail, "Function parameter");
});

test("tracks generic syntax used by the language tour", () => {
    const source = fs.readFileSync(
        path.join(repositoryRoot, "examples/language-tour/main.fdn"),
        "utf8"
    );
    const labels = new Set(collectCompletions(source).map((entry) => entry.label));
    const grammar = fs.readFileSync(
        path.join(extensionRoot, "syntaxes/foundation.tmLanguage.json"),
        "utf8"
    );

    for (const label of ["TourResult", "TourState", "Option", "Result", "identity", "panic"]) {
        assert.ok(labels.has(label));
    }
    assert.match(grammar, /entity\.name\.type\.parameter\.foundation/);
    assert.match(grammar, /punctuation\.definition\.type-arguments\.begin\.foundation/);
    assert.match(grammar, /storage\.modifier\.ownership\.foundation/);

    const parsedGrammar = JSON.parse(grammar);
    const specialization = new RegExp(
        parsedGrammar.repository.genericFunctionApplications.patterns[0].begin
    );
    assert.match("identity<i32>", specialization);
    assert.doesNotMatch("x < y\nlet b = p > (c)", specialization);
});

test("tracks ownership declarations and borrowed parameters", () => {
    const completions = collectCompletions(`
        struct User { id i32 }
        struct Holder { user own User count i32 }
        fn read(user view User) i32 { user.id }
        fn updateUser(user edit User, id i32) void { user.id = id }
        fn main() i32 { 0 }
    `);
    const fields = completions
        .filter((entry) => entry.kind === "Field" && entry.detail === "Field of Holder")
        .map((entry) => entry.label);
    const parameters = completions
        .filter((entry) => entry.kind === "Variable" && entry.detail === "Function parameter")
        .map((entry) => entry.label);

    assert.deepEqual(fields, ["count", "user"]);
    assert.ok(parameters.includes("user"));
    assert.ok(parameters.includes("id"));
});

test("tracks owned place operations and struct patterns", () => {
    const source = `
        struct Pair { Value String Count i32 }
        fn main() i32 {
            var current = "old"
            let previous = replace current with "new"
            let Pair { Value as value Count as count } = Pair { Value = previous Count = 1 }
            count
        }
    `;
    const byLabel = new Map(collectCompletions(source).map((entry) => [entry.label, entry]));
    const grammar = readJson("syntaxes/foundation.tmLanguage.json");
    const snippets = readJson("snippets/foundation.json");

    assert.equal(byLabel.get("value").detail, "Destructured field binding");
    assert.equal(byLabel.get("count").detail, "Destructured field binding");
    assert.equal(byLabel.has("Pair"), true);
    assert.match(grammar.repository.structPatterns.patterns[0].begin, /let/);
    assert.match(grammar.repository.structPatterns.patterns[0].patterns[0].captures[2].name,
        /keyword/);
    assert.equal(snippets["Replace place"].prefix, "replace");
    assert.equal(snippets["Struct destructuring"].prefix, "destructure");
    assert.equal(snippets["Deterministic drop"].prefix, "drop");
    assert.equal(snippets["Standard list"].prefix, "list");
});

test("collects contracts, implementations, and receiver methods", () => {
    const completions = collectCompletions(`
        contract Reader<T> {
            fn Read(view, fallback T) T
        }
        struct Box<T> implements Reader<T> {
            value T
            fn Read(view, fallback T) T { fallback }
            fn Replace(edit, value T) void { self.value = value }
        }
        fn main() i32 { 0 }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("Reader").kind, "Contract");
    assert.equal(byLabel.get("Reader").detail, "Foundation contract<T>");
    assert.equal(byLabel.get("Box").insertText, "Box { value = ${1:value} }");
    assert.equal(byLabel.get("Read").kind, "Method");
    assert.equal(byLabel.get("Read").insertText, "Read(${1:fallback})");
    assert.equal(byLabel.get("Replace").insertText, "Replace(${1:value})");
    assert.equal(byLabel.has("fn"), true);

    const grammar = readJson("syntaxes/foundation.tmLanguage.json");
    assert.match(grammar.repository.contractDefinitions.patterns[0].beginCaptures[2].name,
        /interface/);
    assert.match(grammar.repository.structDefinitions.patterns[0].beginCaptures[6].name,
        /implementation/);
    assert.match(grammar.repository.methodCalls.patterns[0].captures[2].name,
        /method/);
    assert.match(grammar.repository.languageVariables.patterns[0].name, /self/);
    assert.ok(grammar.repository.structDefinitions.patterns[0].patterns.some(
        (pattern) => pattern.include === "#blocks"
    ));
    assert.equal(grammar.repository.blocks.patterns[0].patterns[0].include, "$self");
});

test("collects contract inheritance, defaults, and delegation", () => {
    const completions = collectCompletions(`
        contract Named { fn Name(view) String }
        contract Principal extends Named {
            fn DisplayName(view) String { self.Name() }
        }
        struct Identity implements Principal {
            value String
            fn Name(view) String { self.value }
        }
        struct Admin implements Principal by identity {
            identity Identity
        }
        fn main() i32 { 0 }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("Principal").detail, "Foundation contract extends Named");
    assert.equal(byLabel.get("Admin").detail,
        "Foundation struct implements Principal by identity");
    assert.equal(byLabel.get("DisplayName").detail,
        "Default contract method of Principal");
    assert.equal(byLabel.get("extends").kind, "Keyword");
    assert.equal(byLabel.get("by").kind, "Keyword");

    const grammar = readJson("syntaxes/foundation.tmLanguage.json");
    assert.match(grammar.repository.contractDefinitions.patterns[0].beginCaptures[6].name,
        /inheritance/);
    const snippets = readJson("snippets/foundation.json");
    assert.equal(snippets["Delegated contract implementation"].prefix, "implementsby");
    assert.equal(snippets["Contract default method"].prefix, "contractdefault");
});

test("collects exported contracts and methods across packages", () => {
    const application = `
        package example.app
        import example.reader
        fn main() i32 { 0 }
    `;
    const library = `
        package example.reader
        contract Reader { fn Read(view) i32 fn hidden(view) i32 }
        struct Value implements Reader {
            fn Read(view) i32 { 1 }
            fn hidden(view) i32 { 2 }
        }
    `;
    const completions = collectCompletions(application, [application, library]);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("reader.Reader").kind, "Contract");
    assert.equal(byLabel.get("Read").kind, "Method");
    assert.equal(byLabel.has("hidden"), false);
});

test("ships Result handling and panic snippets", () => {
    const snippets = readJson("snippets/foundation.json");

    assert.equal(snippets["Result binding"].prefix, "constelse");
    assert.equal(snippets["Discard value"].prefix, "discard");
    assert.equal(snippets.Panic.prefix, "panic");
    assert.equal(snippets["New owned value"].prefix, "new");
    assert.equal(snippets["Read parameter"].prefix, "readparam");
    assert.equal(snippets["Edit parameter"].prefix, "editparam");
    assert.equal(snippets["Fixed array binding"].prefix, "array");
    assert.equal(snippets["Read slice parameter"].prefix, "readslice");
    assert.equal(snippets["Edit slice parameter"].prefix, "editslice");
    assert.equal(snippets["Indexed assignment"].prefix, "indexset");
    assert.equal(snippets["Package declaration"].prefix, "package");
    assert.equal(snippets["Package import"].prefix, "import");
    assert.equal(snippets.Contract.prefix, "contract");
    assert.equal(snippets["Generic contract"].prefix, "contractg");
    assert.equal(snippets["Contract implementation"].prefix, "implements");
    assert.equal(snippets["View method"].prefix, "methodview");
    assert.equal(snippets["Edit method"].prefix, "methodedit");
    assert.equal(snippets["Own method"].prefix, "methodown");
    assert.equal(snippets["Methods block"].prefix, "methods");
    assert.equal(snippets["Short guard"].prefix, "guard");
    assert.equal(snippets["Postfix conditional"].prefix, "ifvalue");
    assert.equal(snippets.Service.prefix, "service");
    assert.equal(snippets.Task.prefix, "task");
    assert.equal(snippets["State machine"].prefix, "statemachine");
    assert.equal(snippets["Channel select"].prefix, "select");
    assert.equal(snippets.Pipeline.prefix, "pipeline");
    assert.equal(snippets.Saga.prefix, "saga");
    assert.equal(snippets.Test.prefix, "test");
    assert.equal(snippets["Unsafe block"].prefix, "unsafe");
});

test("tracks accepted bindings and distributed methods", () => {
    const completions = collectCompletions(`
        struct User {
            name String
        }

        methods User {
            fn displayName(self) String { self.name }
            fn rename(&self, name String) void { self.name = name }
        }

        fn main() i32 {
            const user = new User { name = "Mirko" }
            0
        }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("user").detail, "Local binding");
    assert.equal(byLabel.get("displayName").detail, "Distributed method of User");
    assert.equal(byLabel.get("rename").detail, "Distributed method of User");
});

test("collects declarations that use arrays and slices", () => {
    const completions = collectCompletions(`
        struct Batch { names [2]String }
        fn first(names view [String], positions [2]i32) void {
            var local = [1, 2]
            print(names[positions[0]])
        }
    `);
    assert.ok(completions.some((entry) => entry.label === "names" && entry.kind === "Field"));
    assert.ok(completions.some(
        (entry) => entry.label === "positions" && entry.detail === "Function parameter"
    ));
    assert.ok(completions.some(
        (entry) => entry.label === "local" && entry.detail === "Local binding"
    ));
});

test("collects structs and their fields", () => {
    const completions = collectCompletions(`
        struct User {
            id i32
            name String
            group example.Group
        }
        fn main() i32 { 0 }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("User").kind, "Struct");
    assert.equal(
        byLabel.get("User").insertText,
        "User { id = ${1:id} name = ${2:name} group = ${3:group} }"
    );
    assert.equal(byLabel.get("id").detail, "Field of User");
    assert.equal(byLabel.get("name").detail, "Field of User");
    assert.equal(byLabel.get("group").detail, "Field of User");
    assert.equal(byLabel.has("Group"), false);
});

test("collects functions, parameters, and local bindings", () => {
    const completions = collectCompletions(`
        fn add(left i32, right i32) i32 {
            let result = left + right
            var calls = 1
            result
        }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("add").kind, "Function");
    assert.equal(byLabel.get("add").insertText, "add(${1:left}, ${2:right})");
    assert.equal(byLabel.get("left").detail, "Function parameter");
    assert.equal(byLabel.get("right").detail, "Function parameter");
    assert.equal(byLabel.get("result").detail, "Local binding");
    assert.equal(byLabel.get("calls").detail, "Local binding");
});

test("ignores declarations in comments and strings", () => {
    const source = `
        // fn hidden(commented i32) i32 {}
        /*
        fn hidden_in_block(blocked i32) i32 {}
        /* fn hidden_nested(value i32) i32 {} */
        */
        fn visible(actual String) void {
            let text = "fn hidden_in_string(value i32)"
        }
    `;
    const labels = collectCompletions(source).map((entry) => entry.label);

    assert.ok(labels.includes("visible"));
    assert.ok(labels.includes("actual"));
    assert.ok(labels.includes("text"));
    assert.ok(!labels.includes("hidden"));
    assert.ok(!labels.includes("commented"));
    assert.ok(!labels.includes("hidden_in_string"));
    assert.ok(!labels.includes("hidden_in_block"));
    assert.ok(!labels.includes("hidden_nested"));
    assert.ok(!labels.includes("blocked"));
    assert.ok(!labels.includes("value"));
});

test("masks trivia without changing source offsets", () => {
    const source = "/* outer\n/* nested */\n*/\nfn main() void {\n    print(\"escaped \\\" text\") // note\n}\n";
    const masked = maskTrivia(source);

    assert.equal(masked.length, source.length);
    assert.equal(masked.split("\n").length, source.split("\n").length);
    assert.match(masked, /fn main/);
    assert.doesNotMatch(masked, /outer|nested|escaped|note/);
});

test("masks nested attribute applications without changing source offsets", () => {
    const source = "@Route(.GET, Options { fallback = .Some(\"none\") })\nfn main() i32 { 0 }\n";
    const masked = maskAttributeApplications(maskTrivia(source));

    assert.equal(masked.length, source.length);
    assert.equal(masked.split("\n").length, source.split("\n").length);
    assert.doesNotMatch(masked, /Route|fallback|Some/);
    assert.match(masked, /fn main/);
});

test("returns deterministic unique completions", () => {
    const source = "fn same(same i32) i32 { let same = same return same }";
    const first = collectCompletions(source);
    const second = collectCompletions(source);

    assert.deepEqual(first, second);
    assert.equal(
        first.filter((entry) => entry.label === "same" && entry.kind === "Variable").length,
        1
    );
});
