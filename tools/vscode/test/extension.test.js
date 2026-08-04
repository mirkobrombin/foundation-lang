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
    encodeMessage,
    findLanguageServer,
    platformDirectory
} = require("../src/languageClient");

const extensionRoot = path.resolve(__dirname, "..");
const repositoryRoot = path.resolve(extensionRoot, "../..");

function readJson(relativePath) {
    return JSON.parse(fs.readFileSync(path.join(extensionRoot, relativePath), "utf8"));
}

test("registers Foundation source files", () => {
    const manifest = readJson("package.json");
    const language = manifest.contributes.languages.find((value) =>
        value.id === "foundation");
    const packageLanguage = manifest.contributes.languages.find((value) =>
        value.id === "foundation-package");
    const lockLanguage = manifest.contributes.languages.find((value) =>
        value.id === "foundation-lock");
    const grammar = manifest.contributes.grammars.find((value) =>
        value.language === "foundation");

    assert.equal(language.id, "foundation");
    assert.deepEqual(language.extensions, [".fn"]);
    assert.equal(grammar.language, "foundation");
    assert.equal(grammar.scopeName, "source.foundation");
    assert.ok(fs.existsSync(path.join(extensionRoot, grammar.path)));
    assert.ok(fs.existsSync(path.join(extensionRoot, language.configuration)));
    assert.deepEqual(packageLanguage.filenames, ["foundation.package"]);
    assert.deepEqual(lockLanguage.filenames, ["foundation.lock"]);
    for (const value of [packageLanguage, lockLanguage]) {
        assert.ok(fs.existsSync(path.join(extensionRoot, value.configuration)));
        const valueGrammar = manifest.contributes.grammars.find((candidate) =>
            candidate.language === value.id);
        assert.ok(valueGrammar);
        assert.ok(fs.existsSync(path.join(extensionRoot, valueGrammar.path)));
    }

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
    const extensionEntry = fs.readFileSync(
        path.join(extensionRoot, "src/extension.js"),
        "utf8"
    );
    assert.match(packagingScript, /package\.json/);
    assert.match(packagingScript, /foundation-lang-\$version\.vsix/);
    assert.match(packagingScript, /repository_root\/std\/\./);
    assert.match(packagingScript, /repository_root\/foundation\/\./);
    assert.match(packagingScript, /languageClient\.js/);
    assert.match(packagingScript, /foundation-package\.tmLanguage\.json/);
    assert.match(packagingScript, /foundation-lock\.tmLanguage\.json/);
    assert.match(packagingScript, /package-language-configuration\.json/);
    assert.match(languageClient, /registerDocumentSymbolProvider/);
    assert.match(languageClient, /registerWorkspaceSymbolProvider/);
    assert.match(languageClient, /registerCompletionItemProvider/);
    assert.match(languageClient, /registerTextDocumentContentProvider\("foundation-builtin"/);
    assert.match(languageClient, /foundation\/builtinDocument/);
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
    assert.match(languageClient, /editor\.action\.showReferences/);
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
    assert.match(languageClient, /createFileSystemWatcher\("\*\*\/\*\.fn"\)/);
    assert.match(languageClient, /createFileSystemWatcher\(\s*"\*\*\/foundation\.package"/);
    assert.match(languageClient, /createFileSystemWatcher\("\*\*\/foundation\.lock"\)/);
    assert.match(languageClient, /workspace\/didChangeWatchedFiles/);
    assert.equal(manifest.version, "0.124.0");
    assert.equal(manifest.contributes.commands[0].command,
        "foundation.openCompositeType");
    assert.equal(manifest.contributes.commands[1].command,
        "foundation.openTypeDefinition");
    assert.equal(manifest.contributes.commands[2].command,
        "foundation.showOutput");
    assert.equal(
        manifest.contributes.configuration.properties["foundation.languageServer.path"].default,
        ""
    );
    assert.equal(
        manifest.contributes.configuration.properties["foundation.inlayHints.emptyTests"].default,
        true
    );
    assert.match(languageClient, /foundationKind !== "emptyTest"/);
    assert.match(packagingScript, /extension\/bin\/\$platform/);
    assert.match(packagingScript, /FOUNDATION_LANGUAGE_SERVER/);
    assert.match(extensionEntry, /languageClient\.fail\(error\)/);
    assert.match(languageClient, /Server initialized/);
    assert.match(languageClient, /IntelliSense providers registered/);
});

test("recognizes package and lock directives with dedicated scopes", () => {
    const packageGrammar = readJson("syntaxes/foundation-package.tmLanguage.json");
    const lockGrammar = readJson("syntaxes/foundation-lock.tmLanguage.json");

    assert.equal(packageGrammar.scopeName, "source.foundation.package");
    assert.match(packageGrammar.repository.format.patterns[0].match,
        /foundation\\\.package/);
    assert.match(packageGrammar.repository.dependency.patterns[0].match, /dependency/);
    assert.equal(
        packageGrammar.repository.dependency.patterns[0].captures[7].name,
        "constant.language.target.foundation.package"
    );
    assert.equal(
        packageGrammar.repository.dependency.patterns[0].captures[9].name,
        "constant.language.scope.foundation.package"
    );
    assert.equal(
        packageGrammar.repository.testSource.patterns[0].captures[1].name,
        "keyword.declaration.test-source.foundation.package"
    );
    const dependencyTargetScope = new RegExp(
        packageGrammar.repository.dependency.patterns[0].match
    ).exec("dependency example.profile ^1.2.3 path ../profile target linux scope test");
    assert.deepEqual(dependencyTargetScope?.slice(1), [
        "dependency", "example.profile", "^1.2.3", "path", "../profile",
        "target", "linux", "scope", "test"
    ]);
    const dependencyScopeTarget = new RegExp(
        packageGrammar.repository.dependency.patterns[1].match
    ).exec("dependency example.profile ^1.2.3 path ../profile scope test target linux");
    assert.deepEqual(dependencyScopeTarget?.slice(1), [
        "dependency", "example.profile", "^1.2.3", "path", "../profile",
        "scope", "test", "target", "linux"
    ]);
    const testSource = new RegExp(packageGrammar.repository.testSource.patterns[0].match)
        .exec("test_source tests");
    assert.deepEqual(testSource?.slice(1), ["test_source", "tests"]);
    const fcs = new RegExp(packageGrammar.repository.fcs.patterns[0].match)
        .exec("fcs strict");
    assert.deepEqual(fcs?.slice(1), ["fcs", "strict"]);
    assert.equal(lockGrammar.scopeName, "source.foundation.lock");
    assert.match(lockGrammar.repository.format.patterns[0].match,
        /foundation\\\.lock/);
    assert.equal(
        lockGrammar.repository.package.patterns[0].captures[4].name,
        "constant.other.digest.foundation.lock"
    );
    const digest = "a".repeat(64);
    const locked = new RegExp(lockGrammar.repository.package.patterns[0].match)
        .exec(`package example.profile 1.2.3 sha256:${digest} registry default`);
    assert.deepEqual(locked?.slice(1), [
        "package", "example.profile", "1.2.3", `sha256:${digest}`,
        "registry", "default"
    ]);
    assert.equal(
        lockGrammar.repository.edge.patterns[0].captures[5].name,
        "constant.language.scope.foundation.lock"
    );
    const edge = new RegExp(lockGrammar.repository.edge.patterns[0].match)
        .exec("edge example.app example.profile scope test");
    assert.deepEqual(edge?.slice(1), [
        "edge", "example.app", "example.profile", "scope", "test"
    ]);
});

test("prefers the bundled platform language server", () => {
    const root = fs.mkdtempSync(path.join(require("node:os").tmpdir(), "foundation-vscode-"));
    const name = process.platform === "win32" ? "foundation-ls.exe" : "foundation-ls";
    const bundled = path.join(root, "bin", platformDirectory(), name);
    fs.mkdirSync(path.dirname(bundled), { recursive: true });
    fs.writeFileSync(bundled, "server");

    const vscode = {
        workspace: {
            getConfiguration: () => ({ get: () => "" }),
            workspaceFolders: []
        }
    };

    assert.equal(findLanguageServer(vscode, root), bundled);
    fs.rmSync(root, { recursive: true, force: true });
});

test("shows the language server state and output command", () => {
    let command;
    let shown = false;
    const status = { show() { this.visible = true; } };
    const output = {
        show() { shown = true; },
        appendLine() {},
        dispose() {}
    };
    const vscode = {
        StatusBarAlignment: { Right: 2 },
        commands: {
            registerCommand(name, callback) {
                command = { name, callback };
                return { dispose() {} };
            }
        },
        languages: {
            createDiagnosticCollection: () => ({ dispose() {} })
        },
        window: {
            createOutputChannel: () => output,
            createStatusBarItem: () => status
        }
    };
    const client = new FoundationLanguageClient(vscode, {
        subscriptions: [],
        extensionPath: "/extension"
    });

    assert.equal(status.text, "$(sync~spin) Foundation: Starting");
    assert.equal(status.command, "foundation.showOutput");
    assert.equal(status.visible, true);
    assert.equal(command.name, "foundation.showOutput");
    command.callback();
    assert.equal(shown, true);

    client.setStatus("ready", "/extension/bin/foundation-ls");
    assert.equal(status.text, "$(check) Foundation: Ready");
    assert.match(status.tooltip, /foundation-ls/);
});

test("renders compiler-provided builtin documents", async () => {
    const client = Object.create(FoundationLanguageClient.prototype);
    const requests = [];
    client.request = async (method, params) => {
        requests.push({ method, params });
        return { contents: "// Built into the Foundation compiler.\n\nfn print(value String) void\n" };
    };

    const document = await client.builtinDocument({ path: "/print.fn" });
    assert.equal(document, "// Built into the Foundation compiler.\n\nfn print(value String) void\n");
    assert.deepEqual(requests, [{
        method: "foundation/builtinDocument",
        params: { name: "print" }
    }]);
    assert.equal(await client.builtinDocument({ path: "/not a builtin.fn" }), "");
    assert.equal(requests.length, 1);
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
        params: { uri: "file:///tmp/main.fn", diagnostics: [] }
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

test("filters empty-test inlay hints independently", async () => {
    class Position {
        constructor(line, character) {
            this.line = line;
            this.character = character;
        }
    }
    class InlayHint {
        constructor(position, label, kind) {
            this.position = position;
            this.label = label;
            this.kind = kind;
        }
    }
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = {
        Position,
        InlayHint,
        workspace: {
            getConfiguration: () => ({
                get: (name, fallback) => name === "inlayHints.emptyTests" ? false : fallback
            })
        }
    };
    client.request = async () => [
        {
            position: { line: 1, character: 8 },
            label: "is empty",
            kind: 1,
            foundationKind: "emptyTest"
        },
        {
            position: { line: 2, character: 12 },
            label: "value:",
            kind: 2
        }
    ];
    const hints = await client.inlayHints(
        { uri: { toString: () => "file:///main.fn" } },
        { start: { line: 0, character: 0 }, end: { line: 3, character: 0 } },
        undefined
    );

    assert.deepEqual(hints.map((hint) => hint.label), ["value:"]);
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
            uri: "file:///tmp/first.fn",
            range: {
                start: { line: 0, character: 8 },
                end: { line: 0, character: 22 }
            }
        },
        {
            uri: "file:///tmp/second.fn",
            range: {
                start: { line: 0, character: 8 },
                end: { line: 0, character: 22 }
            }
        }
    ];

    const locations = await client.definition(
        { uri: { toString: () => "file:///tmp/main.fn" } },
        new Position(1, 12)
    );

    assert.equal(locations.length, 2);
    assert.equal(locations[0].uri.value, "file:///tmp/first.fn");
    assert.equal(locations[1].uri.value, "file:///tmp/second.fn");
    assert.equal(locations[1].range.start.character, 8);
});

test("maps documented completions and parameter signature help", async () => {
    class MarkdownString {
        constructor(value) {
            this.value = value;
        }

        appendMarkdown(value) {
            this.value += value;
        }
    }
    class CompletionItem {
        constructor(label, kind) {
            Object.assign(this, { label, kind });
        }
    }
    class SnippetString {
        constructor(value) {
            this.value = value;
        }
    }
    class SignatureHelp {}
    class SignatureInformation {
        constructor(label, documentation) {
            Object.assign(this, { label, documentation });
        }
    }
    class ParameterInformation {
        constructor(label, documentation) {
            Object.assign(this, { label, documentation });
        }
    }
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = {
        MarkdownString,
        CompletionItem,
        SnippetString,
        SignatureHelp,
        SignatureInformation,
        ParameterInformation
    };
    client.request = async (method) => {
        if (method === "textDocument/completion") {
            return [{
                label: "Rename",
                kind: 2,
                detail: "fn Rename(&self, name String) void",
                insertText: "Rename($0)",
                insertTextFormat: 2,
                command: {
                    title: "Show parameter information",
                    command: "editor.action.triggerParameterHints"
                },
                documentation: {
                    kind: "markdown",
                    value: "Replaces the displayed profile name."
                }
            }];
        }
        return {
            signatures: [{
                label: "fn Rename(&self, name String) void",
                documentation: {
                    kind: "markdown",
                    value: "Replaces the displayed profile name."
                },
                foundationTypes: [{
                    label: "User",
                    uri: "file:///tmp/user.fn",
                    position: { line: 1, character: 7 }
                }],
                parameters: [{
                    label: "name String",
                    documentation: {
                        kind: "markdown",
                        value: "The new user-facing name."
                    },
                    foundationTypes: [{
                        label: "StringBox",
                        uri: "file:///tmp/string_box.fn",
                        position: { line: 2, character: 7 }
                    }]
                }]
            }],
            activeSignature: 0,
            activeParameter: 0
        };
    };
    const document = { uri: { toString: () => "file:///tmp/main.fn" } };
    const position = { line: 4, character: 9 };

    const completions = await client.completions(document, position);
    const signature = await client.signatureHelp(document, position);

    assert.equal(completions[0].documentation.value,
        "Replaces the displayed profile name.");
    assert.equal(completions[0].insertText.value, "Rename($0)");
    assert.equal(completions[0].command.command,
        "editor.action.triggerParameterHints");
    assert.equal(signature.signatures[0].documentation.value,
        "Replaces the displayed profile name.\n\n**Types**: " +
        "[User](command:foundation.openTypeDefinition?" +
        "%5B%22file%3A%2F%2F%2Ftmp%2Fuser.fn%22%2C%7B%22line%22%3A1%2C" +
        "%22character%22%3A7%7D%5D)");
    assert.deepEqual(signature.signatures[0].documentation.isTrusted, {
        enabledCommands: ["foundation.openTypeDefinition"]
    });
    assert.equal(signature.signatures[0].parameters[0].label, "name String");
    assert.match(signature.signatures[0].parameters[0].documentation.value,
        /\*\*Types\*\*: \[StringBox\]\(command:foundation\.openTypeDefinition/);
});

test("combines textual hover docs with semantic type actions", async () => {
    class MarkdownString {
        constructor(value) {
            this.value = value;
        }

        appendMarkdown(value) {
            this.value += value;
        }
    }
    class Range {
        constructor(startLine, startCharacter, endLine, endCharacter) {
            this.start = { line: startLine, character: startCharacter };
            this.end = { line: endLine, character: endCharacter };
        }
    }
    class Hover {
        constructor(contents, range) {
            Object.assign(this, { contents, range });
        }
    }
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = { Hover, MarkdownString, Range };
    client.request = async () => ({
        contents: {
            kind: "markdown",
            value: "Resolves `missingSymbol` as ordinary prose."
        },
        range: {
            start: { line: 3, character: 4 },
            end: { line: 3, character: 11 }
        },
        foundationTypes: [{
            label: "User",
            uri: "file:///tmp/user.fn",
            position: { line: 1, character: 7 }
        }],
        foundationComposite: {
            uri: "file:///tmp/user.fn",
            position: { line: 1, character: 7 }
        }
    });

    const hover = await client.hover(
        { uri: { toString: () => "file:///tmp/main.fn" } },
        { line: 3, character: 5 }
    );

    assert.match(hover.contents.value, /`missingSymbol` as ordinary prose/);
    assert.match(hover.contents.value,
        /\[User\]\(command:foundation\.openTypeDefinition/);
    assert.doesNotMatch(hover.contents.value, /Peek Composite Type/);
    assert.deepEqual(hover.contents.isTrusted, {
        enabledCommands: ["foundation.openTypeDefinition"]
    });
});

test("opens compiler-resolved nominal types", async () => {
    class Position {
        constructor(line, character) {
            Object.assign(this, { line, character });
        }
    }
    class Range {
        constructor(start, end) {
            Object.assign(this, { start, end });
        }
    }
    class Selection {
        constructor(anchor, active) {
            Object.assign(this, { anchor, active });
        }
    }
    const revealed = [];
    const target = { uri: "file:///project/profile/user.fn" };
    const editor = {
        revealRange(range) {
            revealed.push(range);
        }
    };
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = {
        Position,
        Range,
        Selection,
        TextEditorRevealType: { InCenterIfOutsideViewport: 2 },
        Uri: { parse: (value) => ({ value }) },
        workspace: {
            openTextDocument: async (uri) => {
                target.opened = uri;
                return target;
            }
        },
        window: {
            showTextDocument: async (document) => {
                target.shown = document;
                return editor;
            }
        }
    };

    await client.openTypeDefinition(
        "file:///project/profile/user.fn",
        { line: 4, character: 7 }
    );

    assert.equal(target.opened.value, target.uri);
    assert.equal(target.shown, target);
    assert.deepEqual(editor.selection, new Selection(
        new Position(4, 7),
        new Position(4, 7)
    ));
    assert.equal(revealed.length, 1);
    assert.equal(revealed[0].start.line, 4);
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
    const document = { uri: { toString: () => "file:///tmp/main.fn" } };

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
    const document = { uri: { toString: () => "file:///tmp/main.fn" } };
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
                    "file:///tmp/main.fn": [{
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
    const document = { uri: { toString: () => "file:///tmp/main.fn" } };
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
    client.request = async () => [
        {
            range: {
                start: { line: 1, character: 3 },
                end: { line: 1, character: 6 }
            },
            command: {
                title: "Peek Composite Type",
                command: "foundation.openCompositeType",
                arguments: ["file:///tmp/main.fn", { line: 1, character: 3 }]
            }
        },
        {
            range: {
                start: { line: 1, character: 3 },
                end: { line: 1, character: 6 }
            },
            command: {
                title: "1 reference",
                command: "editor.action.showReferences",
                arguments: [
                    "file:///tmp/main.fn",
                    { line: 1, character: 3 },
                    [{
                        uri: "file:///tmp/main.fn",
                        range: {
                            start: { line: 2, character: 12 },
                            end: { line: 2, character: 15 }
                        }
                    }]
                ]
            }
        }
    ];

    const lenses = await client.codeLenses({
        uri: { toString: () => "file:///tmp/main.fn" }
    });

    assert.equal(lenses.length, 2);
    assert.equal(lenses[0].command.title, "Peek Composite Type");
    assert.equal(lenses[0].command.arguments[0].value, "file:///tmp/main.fn");
    assert.deepEqual(lenses[0].command.arguments[1], new Position(1, 3));
    assert.equal(lenses[1].command.title, "1 reference");
    assert.equal(lenses[1].command.arguments[2][0].range.start.line, 2);
});

test("opens composite sources in the native peek editor", async () => {
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
    const calls = [];
    const client = Object.create(FoundationLanguageClient.prototype);
    client.vscode = {
        Position,
        Range,
        Location,
        Uri: {
            parse: (value) => ({ value, toString: () => value })
        },
        commands: {
            executeCommand: async (...args) => calls.push(args)
        },
        window: { showErrorMessage() {} }
    };
    client.requestCompositeType = async () => ({
        fragments: [{
            key: "struct-prefix",
            kind: "struct",
            uri: "file:///project/profile/user.fn",
            range: {
                start: { line: 0, character: 0 },
                end: { line: 3, character: 1 }
            }
        }, {
            key: "method:rename",
            kind: "method",
            uri: "file:///project/profile/rename.fn",
            range: {
                start: { line: 2, character: 0 },
                end: { line: 5, character: 1 }
            }
        }, {
            key: "struct-suffix",
            kind: "struct",
            uri: "file:///project/profile/user.fn",
            range: {
                start: { line: 3, character: 0 },
                end: { line: 3, character: 1 }
            }
        }]
    });

    await client.openCompositeType("file:///project/profile/user.fn", new Position(0, 7));

    assert.equal(calls.length, 1);
    assert.equal(calls[0][0], "editor.action.showReferences");
    assert.equal(calls[0][1].value, "file:///project/profile/user.fn");
    assert.deepEqual(calls[0][2], new Position(0, 7));
    assert.equal(calls[0][3].length, 2);
    assert.equal(calls[0][3][1].uri.value, "file:///project/profile/rename.fn");
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
        uri: "file:///tmp/main.fn",
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
            uri: "file:///tmp/main.fn"
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
        uri: "file:///tmp/main.fn",
        range,
        selectionRange: range,
        data: {
            kind: "function",
            name: "add",
            scope: "function:sample",
            uri: "file:///tmp/main.fn"
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
    const recognizedKeywords = [...lexer.matchAll(/text == "([A-Za-z_][A-Za-z0-9_]*)"/g)]
        .map((match) => match[1]);
    const compilerKeywords = recognizedKeywords.filter((keyword) => keyword !== "let");
    const completionLabels = new Set(staticCompletions.map((entry) => entry.label));

    assert.deepEqual(compilerKeywords, [
        "package",
        "import",
        "as",
        "extern",
        "struct",
        "service",
        "methods",
        "enum",
        "state_machine",
        "pipeline",
        "saga",
        "contract",
        "attribute",
        "implements",
        "extends",
        "delegate",
        "fn",
        "action",
        "task",
        "unsafe",
        "spawn",
        "const",
        "var",
        "return",
        "discard",
        "if",
        "else",
        "while",
        "for",
        "in",
        "break",
        "continue",
        "select",
        "timeout",
        "match",
        "capture",
        "replace",
        "with",
        "new",
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
    assert.ok(recognizedKeywords.includes("let"));
    assert.equal(completionLabels.has("let"), false);
    assert.doesNotMatch(grammar, /\\blet\\b/);

    for (const type of [
        "i32", "u64", "bool", "String", "void", "Option", "Result", "ChannelError",
        "Task", "Channel", "Sender", "Receiver", "send", "receive", "clone",
        "TcpConnection", "TcpListener", "TcpReader", "TcpWriter", "StreamPair"
    ]) {
        assert.match(grammar, new RegExp(`\\b${type}\\b`));
        assert.ok(completionLabels.has(type));
    }
    for (const keyword of [
        "const", "methods", "delegate", "service", "action", "state_machine", "pipeline",
        "saga", "step", "using", "retry", "exponential", "max", "compensate", "task",
        "spawn", "select", "test", "unsafe", "new", "for", "in"
    ]) {
        assert.match(grammar, new RegExp(`\\b${keyword}\\b`));
        assert.ok(completionLabels.has(keyword));
    }
    for (const type of [
        "i8", "i16", "i64", "u8", "u16", "u32", "f32", "f64", "isize", "usize",
        "never", "UUID", "NumberError"
    ]) {
        assert.match(grammar, new RegExp(`\\b${type}\\b`));
        assert.ok(completionLabels.has(type));
    }
    assert.ok(completionLabels.has("c"));
    assert.ok(completionLabels.has("@target(...)"));
    assert.ok(completionLabels.has("@blocking"));
    const builtinPattern = JSON.parse(grammar).repository.builtins.patterns[0].match;
    assert.match(builtinPattern, /\brange\b/);
    assert.match(builtinPattern, /null/);
    assert.match(builtinPattern, /isNull/);
    for (const builtin of ["print", "panic", "len", "null", "isNull", "channel"]) {
        assert.equal(completionLabels.has(builtin), false);
    }
    assert.ok(completionLabels.has("range"));
    assert.ok(completionLabels.has("expect"));
    assert.ok(completionLabels.has("fail"));
    assert.ok(completionLabels.has("pass"));
    assert.ok(completionLabels.has("@callback"));
    assert.ok(completionLabels.has("targets(...)"));
    assert.ok(completionLabels.has("foundation.di"));
    assert.ok(completionLabels.has("foundation.actions"));
    assert.ok(completionLabels.has("@di.Inject()"));
    assert.ok(completionLabels.has("@actions.Name(...)"));
    assert.ok(completionLabels.has("repeatable"));
    for (const standard of [
        "std.platform", "platform.Current", "platform.Name",
        "std.env", "env.Get", "env.Home",
        "std.text", "text.ByteLen", "text.Contains", "text.NewBuilder",
        "std.path", "path.Join",
        "std.parse", "parse.Bool", "parse.F32", "parse.F64", "parse.I8", "parse.I16", "parse.I32", "parse.I64",
        "parse.Isize", "parse.U8", "parse.U16", "parse.U32", "parse.U64",
        "parse.Usize",
        "std.fs", "fs.OpenLines", "fs.ReadText", "fs.ReadTextLimited", "fs.OpenDir", "fs.Size", "fs.Modified",
        "fs.LineReader.Next", "fs.LineReader.NextLimited",
        "std.net", "net.Listen", "net.Accept", "net.Connect", "net.TcpConnection.Split",
        "net.TcpConnection.PeerAddress", "net.ReadLine",
        "net.ReadLineLimited", "net.ReadExact", "net.WriteAll",
        "std.format", "format.Bool", "format.I8", "format.I16", "format.I32",
        "format.I64", "format.Isize", "format.U8", "format.U16", "format.U32",
        "format.U64", "format.Usize", "format.F32", "format.F64",
        "std.json", "json.Parse",
        "std.time", "time.Now", "time.FromUnix", "time.MonotonicNow",
        "time.Nanoseconds", "time.Milliseconds", "time.Seconds",
        "time.Duration.Parse", "time.Duration.Seconds", "time.Duration.Nanoseconds",
        "time.Duration.IsNegative", "time.Instant.FormatUtc",
        "std.concurrent", "@concurrent.Transferable()", "concurrent.Transferable",
        "concurrent.NewCancellationSource",
        "concurrent.CancellationSource.Token", "concurrent.CancellationSource.Cancel",
        "concurrent.Cancellation.IsRequested",
        "std.bytes", "bytes.Bytes", "bytes.Error", "bytes.FromText",
        "bytes.EncodeBase64URL", "bytes.DecodeBase64URL", "bytes.HmacSha256",
        "bytes.ConstantTimeEqual", "bytes.Bytes.Copy", "bytes.Bytes.Len",
        "bytes.Bytes.At", "bytes.Bytes.Text", "bytes.Bytes.Close",
        "std.pattern", "pattern.IsValid", "pattern.Matches",
        "foundation.hosting", "hosting.Host", "hosting.HostedService",
        "hosting.BackgroundService", "hosting.RunReport", "hosting.RunReason", "hosting.State",
        "hosting.NewHost", "hosting.Run", "hosting.Host.Add",
        "hosting.Host.AddBackground", "hosting.Host.OnStart", "hosting.Host.OnStop",
        "hosting.Host.Start", "hosting.Host.Shutdown", "hosting.Host.NextBackground",
        "foundation.health", "health.Registry", "health.Checker", "health.Report",
        "health.NamedReport", "health.Status", "health.NewRegistry", "health.NewReport",
        "health.StatusText", "health.Registry.Register", "health.Registry.CheckAll",
        "foundation.plugin", "plugin.Plugin", "plugin.NativePlugin", "plugin.Registry",
        "plugin.FactoryRegistry", "plugin.ExecSandbox",
        "plugin.ErrorKind", "plugin.Error", "plugin.NamedError",
        "plugin.RegistrationFailure", "plugin.StartFailure", "plugin.FactoryErrorKind",
        "plugin.FactoryRegistrationFailure", "plugin.SandboxErrorKind", "plugin.SandboxError",
        "plugin.SandboxStartOutcome", "plugin.SandboxStopOutcome", "plugin.LoadNative",
        "plugin.NewRegistry", "plugin.NativePlugin.Name", "plugin.NativePlugin.Start",
        "plugin.NativePlugin.Stop", "plugin.NativePlugin.Close",
        "plugin.NativePlugin.IsRunning", "plugin.Registry.Register",
        "plugin.Registry.StartAll", "plugin.Registry.StopAll", "plugin.Registry.Names",
        "plugin.NewFactoryRegistry", "plugin.NewExecSandbox", "plugin.StartSandbox",
        "plugin.StopSandbox", "plugin.FactoryRegistry.Register",
        "plugin.FactoryRegistry.Create", "plugin.ExecSandbox.Argument",
        "plugin.ExecSandbox.IsRunning", "plugin.ExecSandbox.Close",
        "foundation.pipeline", "pipes.Builder", "pipes.Pipeline", "pipes.Middleware",
        "pipes.New", "pipes.Builder.Use", "pipes.Builder.UseStateful",
        "pipes.Builder.Then", "pipes.Pipeline.Process",
        "foundation.bind", "bind.Values", "bind.Entry", "bind.SourceEntry", "bind.Sources",
        "bind.Error", "bind.ErrorKind", "bind.NewValues", "bind.NewSources", "bind.Append",
        "bind.JsonObject", "bind.JsonSyntaxError", "bind.JsonUnknownField",
        "bind.CopyJsonText", "bind.CopyJsonBool", "bind.CopyJsonNumber",
        "bind.CopyJsonTextList",
        "bind.Values.Set", "bind.Values.Value", "bind.Values.Contains", "bind.Values.Required",
        "bind.Sources.Set", "bind.Sources.Value", "bind.Sources.CopyInto",
        "json.Object.FirstKey",
        "foundation.validation", "validation.ErrorKind", "validation.Error",
        "validation.Errors", "validation.NewErrors", "validation.IsEmail",
        "validation.Errors.IsEmpty", "validation.Errors.Len", "validation.Errors.TakeFirst",
        "foundation.web", "web.Server", "web.Router", "web.RouteTable", "web.RouteMatch",
        "web.Handler", "web.Middleware", "web.Application", "web.Request", "web.Response", "web.Method",
        "web.MatchError", "web.DispatchError", "web.MiddlewareRegistrationError",
        "web.ServeOutcome", "web.NewServer",
        "web.NewRouter", "web.NewRouteTable", "web.Empty", "web.Text", "web.Json", "web.Router.Map",
        "web.Router.Use", "web.Router.UseStateful", "web.Router.UseGroup",
        "web.Router.UseGroupStateful", "web.Router.UseRoute", "web.Router.UseRouteStateful",
        "web.RouteTable.Add", "web.RouteTable.Match", "web.Request.Param", "web.Request.Query",
        "web.Request.Header", "web.Request.Form", "web.Request.IsJSON",
        "web.Response.Header", "web.Response.SetHeader", "web.Response.AddHeader",
        "web.Application.ErrorResponse", "web.Server.ConfigureCORS", "web.Server.ServeOne",
        "foundation.auth", "auth.Payload", "auth.Claims", "auth.StandardClaims", "auth.Error", "auth.Key", "auth.Service",
        "auth.SignedToken", "auth.SignToken", "auth.VerifyToken",
        "auth.ValidateHMACSecret", "auth.NewHMACKey", "auth.NewService",
        "auth.Service.AddKey", "auth.Service.Sign", "auth.Service.Verify",
        "foundation.auth.web", "authWeb.AuthenticatedHandler", "authWeb.Bearer",
        "authWeb.BearerError", "authWeb.Protect",
        "foundation.resiliency", "foundation.resiliency.web",
        "resiliency.RateLimiter", "resiliency.ConfigurationError",
        "resiliency.NewRateLimiter", "resiliency.RateLimiter.Allow",
        "resiliency.RateLimiter.AllowAt", "resiliency.Wait",
        "resiliency.CircuitBreaker", "resiliency.NewCircuitBreaker",
        "resiliency.RetryOptions", "resiliency.Retry",
        "resiliency.Bulkhead", "resiliency.NewBulkhead",
        "rateWeb.RateLimit",
        "rateWeb.RateLimitWithPolicy",
        "std.ring", "ring.Buffer", "ring.ByteBuffer", "ring.ConfigurationError",
        "ring.PushError", "ring.New", "ring.NewBytes", "ring.Buffer.Cap",
        "ring.Buffer.Len", "ring.Buffer.Space", "ring.Buffer.IsEmpty", "ring.Buffer.Push",
        "ring.Buffer.Pop", "ring.Buffer.Peek", "ring.Buffer.Drain", "ring.Buffer.Reset",
        "ring.ByteBuffer.Write", "ring.ByteBuffer.Read", "ring.ByteBuffer.Reset",
        "std.safemap", "safemap.Error", "safemap.Pair", "safemap.Handle", "safemap.Map",
        "safemap.ShardedMap", "safemap.New", "safemap.NewSharded", "safemap.StringEqual",
        "safemap.CloneString", "safemap.StringHasher", "safemap.Handle.Clone",
        "safemap.Map.Handle", "safemap.Map.Set", "safemap.Map.Get", "safemap.Map.Delete",
        "safemap.Map.Has", "safemap.Map.Len", "safemap.Map.Keys", "safemap.Map.Values",
        "safemap.Map.Snapshot", "safemap.Map.Clear", "safemap.Map.GetOrSet",
        "safemap.Map.Compute", "safemap.ShardedMap.WithExpiry",
        "foundation.lock", "lock.Error", "lock.Handle", "lock.Lease", "lock.Locker",
        "lock.InMemoryLocker",
        "lock.New", "lock.Handle.Clone", "lock.Handle.Acquire", "lock.Handle.TryLock",
        "lock.Lease.Key", "lock.Lease.Release", "lock.InMemoryLocker.Handle",
        "lock.Locker.Acquire", "lock.Locker.TryLock",
        "foundation.worker", "worker.Supervisor", "worker.NewSupervisor",
        "worker.Supervisor.Start", "worker.Supervisor.Shutdown", "worker.Supervisor.Cancel",
        "worker.Group", "worker.GroupNext", "worker.GroupWait", "worker.GroupWake",
        "worker.GroupError", "worker.NewGroup", "worker.Group.Add", "worker.Group.Next",
        "worker.Group.WaitOrStop", "worker.Group.Shutdown", "worker.Group.Cancel",
        "worker.Pool", "worker.NewPool", "worker.Pool.Start", "worker.Pool.Shutdown",
        "worker.Pool.Cancel"
    ]) {
        assert.ok(completionLabels.has(standard));
    }
    const webInject = staticCompletions.find((completion) =>
        completion.label === "@web.Inject()");
    assert.equal(webInject?.insertText, "@web.Inject()");
    for (const attribute of ["@bind.Bindable()", "@bind.Name(...)", "@bind.Ignore()",
        "@bind.From(...)", "@bind.Default(...)", "@bind.JsonName(...)", "@bind.JSON()"]) {
        assert.ok(completionLabels.has(attribute));
    }
    for (const attribute of ["@validation.Validatable()", "@validation.Required()",
        "@validation.Min(...)", "@validation.Max(...)", "@validation.Email()"]) {
        assert.ok(completionLabels.has(attribute));
    }
    for (const attribute of ["@web.GlobalMiddleware(...)",
        "@web.GroupMiddleware(...)", "@web.RouteMiddleware(...)"]) {
        assert.ok(completionLabels.has(attribute));
    }
    const bindAppend = staticCompletions.find((completion) =>
        completion.label === "bind.Append");
    assert.equal(bindAppend?.insertText, "bind.Append(&${1:values}, \\$${2:value})");
    const bindSet = staticCompletions.find((completion) =>
        completion.label === "bind.Values.Set");
    assert.equal(bindSet?.insertText, "Set(\\$${1:key}, \\$${2:value})");
    const bindSourcesSet = staticCompletions.find((completion) =>
        completion.label === "bind.Sources.Set");
    assert.equal(bindSourcesSet?.insertText,
        "Set(\\$${1:source}, \\$${2:key}, \\$${3:value})");
    const webEmpty = staticCompletions.find((completion) =>
        completion.label === "web.Empty");
    assert.deepEqual(webEmpty, {
        label: "web.Empty",
        kind: "Function",
        detail: "fn Empty(status i32) web.Response",
        insertText: "web.Empty(${1:204})"
    });
    const webResponseHeader = staticCompletions.find((completion) =>
        completion.label === "web.Response.Header");
    assert.deepEqual(webResponseHeader, {
        label: "web.Response.Header",
        kind: "Method",
        detail: "fn Header(&self, name String) Option<String>",
        insertText: "Header(${1:name})"
    });
    const webResponseSetHeader = staticCompletions.find((completion) =>
        completion.label === "web.Response.SetHeader");
    assert.deepEqual(webResponseSetHeader, {
        label: "web.Response.SetHeader",
        kind: "Method",
        detail: "fn SetHeader(&self, name String, value String) void",
        insertText: "SetHeader(${1:name}, ${2:value})"
    });
    const webResponseAddHeader = staticCompletions.find((completion) =>
        completion.label === "web.Response.AddHeader");
    assert.deepEqual(webResponseAddHeader, {
        label: "web.Response.AddHeader",
        kind: "Method",
        detail: "fn AddHeader(&self, name String, value String) void",
        insertText: "AddHeader(${1:name}, ${2:value})"
    });
    const webServerCORS = staticCompletions.find((completion) =>
        completion.label === "web.Server.ConfigureCORS");
    assert.deepEqual(webServerCORS, {
        label: "web.Server.ConfigureCORS",
        kind: "Method",
        detail: "fn ConfigureCORS(&self, allowOrigins [String]) void",
        insertText: "ConfigureCORS(${1:allowOrigins})"
    });
    const webServerServeOne = staticCompletions.find((completion) =>
        completion.label === "web.Server.ServeOne");
    assert.deepEqual(webServerServeOne, {
        label: "web.Server.ServeOne",
        kind: "Method",
        detail: "fn ServeOne($self) Task<own web.ServeOutcome<E>>",
        insertText: "ServeOne()"
    });
    const parsedGrammar = JSON.parse(grammar);
    const rawPointer = parsedGrammar.repository.types.patterns[0];
    const rawPointerMatch = new RegExp(rawPointer.match).exec("*const i32");
    assert.deepEqual(rawPointerMatch?.slice(1), ["*", "const "]);
    assert.equal(rawPointer.captures[1].name,
        "storage.modifier.pointer.foundation");
    assert.equal(rawPointer.captures[2].name,
        "storage.modifier.readonly.foundation");
    const floatPattern = new RegExp(parsedGrammar.repository.numbers.patterns[0].match);
    const integerPattern = new RegExp(parsedGrammar.repository.numbers.patterns[1].match);
    assert.equal(floatPattern.exec("const value = 1.25e-3")?.[0], "1.25e-3");
    assert.equal(floatPattern.test("timeout 1.seconds"), false);
    assert.equal(integerPattern.exec("timeout 1.seconds")?.[0], "1");
    const targetAttribute = parsedGrammar.repository.compilerAttributes.patterns[0];
    assert.match(targetAttribute.match, /target/);
    assert.match(targetAttribute.captures[4].name, /target/);
    const blockingAttribute = parsedGrammar.repository.compilerAttributes.patterns[1];
    assert.match(blockingAttribute.match, /blocking/);
    const callbackAttribute = parsedGrammar.repository.compilerAttributes.patterns[2];
    assert.match(callbackAttribute.match, /callback/);
    assert.match(callbackAttribute.match, /cancel/);
    assert.match(parsedGrammar.repository.attributeDefinitions.patterns[0].begin, /attribute/);
    assert.match(parsedGrammar.repository.attributeApplications.patterns[0].begin, /@/);
    const enumPayloadPattern = parsedGrammar.repository.enumDefinitions.patterns[0].patterns
        .find((pattern) => pattern.captures?.[1]?.name ===
            "variable.parameter.enum-payload.foundation");
    assert.equal(new RegExp(enumPayloadPattern.match).exec("Stop(code i32)")?.[1], "code");
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
    assert.ok(completionLabels.has("transferable"));
    assert.ok(completionLabels.has("transferable fn(...) R"));
    const snippets = readJson("snippets/foundation.json");
    assert.equal(snippets["Validated model"].prefix, "validmodel");
    assert.match(snippets["Validated model"].body.join("\n"),
        /@validation\.Validatable\(\)/);
    assert.match(snippets["Documented OpenAPI route"].body.join("\n"),
        /@openapi\.Response/);
    assert.match(snippets["Target declaration"].body.join("\n"), /@target/);
    assert.match(snippets["Blocking C ABI import"].body.join("\n"), /@blocking/);
    assert.match(snippets["Callback C ABI import"].body.join("\n"), /@callback/);
    assert.match(snippets["Typed attribute declaration"].body, /targets/);
    assert.match(snippets["Typed attribute application"].body, /@/);
    assert.match(snippets["Read environment value"].body.join("\n"), /env\.Get\(/);
    assert.doesNotMatch(snippets["Read environment value"].body.join("\n"), /\blet\b|\bview\b/);
    assert.match(snippets["Join path"].body, /path\.Join\(/);
    assert.match(snippets["Open line reader"].body.join("\n"), /fs\.OpenLines\(/);
    assert.match(snippets["Read text task"].body.join("\n"), /spawn fs\.ReadText\(/);
    assert.match(snippets["Read text task"].body.join("\n"), /\$.*\.wait\(\)/);
    assert.match(snippets["TCP client tasks"].body.join("\n"), /spawn net\.Connect\(/);
    assert.match(snippets["TCP client tasks"].body.join("\n"), /\.Split\(\)/);
    assert.match(snippets["Parse JSON value"].body.join("\n"), /json\.Parse\(/);
    assert.match(snippets["String builder"].body.join("\n"), /text\.NewBuilder/);
    assert.match(snippets["Format UTC time"].body.join("\n"), /FormatUtc/);
    assert.match(snippets["Main function with arguments"].body.join("\n"),
        /main\(args \[String\]\) i32/);
    assert.equal(parsedGrammar.repository.comments.patterns[0].name,
        "comment.line.double-slash.foundation");
    assert.equal(parsedGrammar.repository.comments.patterns.length, 2);
    const blockPatterns = parsedGrammar.repository.comments.patterns.filter((pattern) =>
        pattern.name?.startsWith("comment.block.")
    );
    assert.ok(blockPatterns.every((pattern) =>
        pattern.patterns?.[0]?.include === "#nestedBlockComments"
    ));
    assert.ok(parsedGrammar.repository.nestedBlockComments.patterns.every((pattern) =>
        pattern.name.startsWith("comment.block.")
    ));
    const commentAwareContexts = [
        "attributeDefinitions",
        "attributeApplications",
        "attributeArgumentParens",
        "blocks",
        "structDefinitions",
        "cAbiDeclarations",
        "contractDefinitions",
        "enumDefinitions",
        "functionDefinitions",
        "functionTypes",
        "captureClauses",
        "structPatterns",
        "genericTypeApplications",
        "genericFunctionApplications"
    ];
    for (const name of commentAwareContexts) {
        const context = parsedGrammar.repository[name].patterns.find((pattern) =>
            pattern.begin && Array.isArray(pattern.patterns)
        );
        assert.equal(context.patterns[0].include, "#comments", name);
    }
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
        "fn NextLimited(&self, limit u64) Result<Option<String>, fs.Error>"
    );
    assert.equal(
        findHover(source, "ReadText").detail,
        "task ReadText(path String) Result<String, fs.Error>"
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
        fn apply(value i32, operation fn(i32) i32) i32 {
            operation(value)
        }
        fn main() i32 {
            const factor = 2
            const scale fn(i32) i32 = fn(value i32) i32 capture(factor) {
                value * factor
            }
            scale(21) - 42
        }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("capture").kind, "Keyword");
    assert.equal(byLabel.get("Callback").insertText, "Callback { call = ${1:call} }");
    assert.match(grammar.repository.functionTypes.patterns[0].begin, /fn/);
    assert.match(grammar.repository.functionTypes.patterns[0].begin, /transferable/);
    assert.match(grammar.repository.functionTypes.patterns[0].beginCaptures[1].name,
        /concurrency/);
    const constrainedParameters = grammar.repository.functionTypeParameters.patterns[0];
    assert.match(constrainedParameters.patterns.find((pattern) =>
        pattern.match === "\\btransferable\\b"
    ).name, /concurrency/);
    assert.match(grammar.repository.functionDefinitions.patterns[0].patterns.find((pattern) =>
        pattern.include === "#functionTypeParameters"
    ).include, /functionTypeParameters/);
    const inferred = grammar.repository.inferredClosureParameters.patterns[0];
    const inferredBegin = new RegExp(inferred.begin);
    assert.match("fn(value) capture(factor) {", inferredBegin);
    assert.match("fn(&counter) {", inferredBegin);
    assert.doesNotMatch("fn(i32) i32", inferredBegin);
    assert.match(grammar.repository.captureClauses.patterns[0].begin, /capture/);
    assert.ok(grammar.repository.captureClauses.patterns[0].begin.includes("\\("));
    assert.equal(grammar.repository.captureClauses.patterns[0].end, "\\)");
    assert.match(grammar.repository.captureClauses.patterns[0].beginCaptures[1].name, /keyword/);
    assert.match(grammar.repository.captureClauses.patterns[0].patterns.find((pattern) =>
        pattern.name === "variable.other.capture.foundation"
    ).name, /capture/);
    assert.equal(snippets["Function value type"].prefix, "fntype");
    assert.equal(snippets["Transferable function value type"].prefix, "transferfn");
    assert.equal(snippets.Closure.prefix, "closure");
    assert.equal(snippets["Contextually inferred closure"].prefix, "closureinfer");
    assert.equal(snippets["Owning closure"].prefix, "closureown");
});

test("collects enums and dot-qualified variants", () => {
    const completions = collectCompletions(`
        enum Outcome {
            Empty
            Value(item i32)
        }
        fn main() i32 { 0 }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("Outcome").kind, "Enum");
    assert.equal(byLabel.get("Outcome.Empty").insertText, "Outcome.Empty");
    assert.equal(byLabel.get("Outcome.Value").insertText, "Outcome.Value(${1:item})");
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
        enum Status<T> { Ready Value(item T) hidden }
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
        "math.Status<${1:T}>.Value(${2:item})"
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
        path.join(repositoryRoot, "examples/language-tour/main.fn"),
        "utf8"
    );
    const labels = new Set(collectCompletions(source).map((entry) => entry.label));
    const grammar = fs.readFileSync(
        path.join(extensionRoot, "syntaxes/foundation.tmLanguage.json"),
        "utf8"
    );

    for (const label of ["TourResult", "TourState", "Option", "Result", "identity"]) {
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
        fn read(user User) i32 { user.id }
        fn updateUser(&user User, id i32) void { user.id = id }
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
            const previous = replace current with "new"
            const Pair { Value as value Count as count } = Pair { Value = previous Count = 1 }
            count
        }
    `;
    const byLabel = new Map(collectCompletions(source).map((entry) => [entry.label, entry]));
    const grammar = readJson("syntaxes/foundation.tmLanguage.json");
    const snippets = readJson("snippets/foundation.json");

    assert.equal(byLabel.get("value").detail, "Destructured field binding");
    assert.equal(byLabel.get("count").detail, "Destructured field binding");
    assert.equal(byLabel.has("Pair"), true);
    assert.match(grammar.repository.structPatterns.patterns[0].begin, /const/);
    const fieldPattern = grammar.repository.structPatterns.patterns[0].patterns.find(
        (pattern) => pattern.captures?.[2]
    );
    assert.match(fieldPattern.captures[2].name, /keyword/);
    assert.equal(snippets["Replace place"].prefix, "replace");
    assert.equal(snippets["Struct destructuring"].prefix, "destructure");
    assert.equal(snippets["Deterministic drop"].prefix, "drop");
    assert.equal(snippets["Standard list"].prefix, "list");
});

test("collects contracts, implementations, and receiver methods", () => {
    const completions = collectCompletions(`
        contract Reader<T> {
            fn Read(self, fallback T) T
        }
        struct Box<T> implements Reader<T> {
            value T
            fn Read(self, fallback T) T { fallback }
            fn Replace(&self, value T) void { self.value = value }
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
    assert.ok(grammar.repository.blocks.patterns[0].patterns.some(
        (pattern) => pattern.include === "$self"
    ));
});

test("highlights guarded match patterns and wildcard fallbacks", () => {
    const grammar = readJson("syntaxes/foundation.tmLanguage.json");
    const [wildcard, guarded, variant] = grammar.repository.matchPatterns.patterns;

    assert.equal(wildcard.captures[1].name, "variable.language.wildcard.foundation");
    assert.equal(wildcard.captures[2].name, "punctuation.separator.foundation");
    assert.deepEqual(new RegExp(wildcard.match).exec("    _:")?.slice(1), ["_", ":"]);
    assert.equal(guarded.captures[1].name, "variable.other.enummember.foundation");
    assert.equal(guarded.captures[5].name, "keyword.control.foundation");
    assert.equal(guarded.captures[6].name, "variable.other.match.guard.foundation");
    assert.deepEqual(
        new RegExp(guarded.match).exec("    Some(value) if predicate: result")?.slice(1),
        ["Some", "(", "value", ")", "if", "predicate", ":"]
    );
    assert.deepEqual(new RegExp(variant.match).exec("    Some(value): result")?.slice(1),
        ["Some", "(", "value", ")", ":"]);
    assert.equal(new RegExp(guarded.match).test("    .Some(value) if predicate: result"), false);

    const snippets = readJson("snippets/foundation.json");
    assert.equal(snippets["Match guard"].prefix, "matchguard");
    assert.deepEqual(snippets["Match guard"].body, [
        "match ${1:value} {",
        "    ${2:Some}(${3:item}) if ${4:predicate}: ${5:result}",
        "    _: ${6:fallback}",
        "}"
    ]);
});

test("collects contract inheritance, defaults, and delegation", () => {
    const completions = collectCompletions(`
        contract Named { fn Name(self) String }
        contract Principal extends Named {
            fn DisplayName(self) String { self.Name() }
        }
        struct Identity implements Principal {
            value String
            fn Name(self) String { self.value }
        }
        struct Admin implements Principal {
            identity Identity
            delegate identity as Principal
        }
        fn main() i32 { 0 }
    `);
    const completionByLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(completionByLabel.get("Principal").detail,
        "Foundation contract extends Named");
    assert.equal(completionByLabel.get("Admin").detail,
        "Foundation struct implements Principal; delegates Principal to identity");
    assert.equal(completionByLabel.get("DisplayName").detail,
        "Default contract method of Principal");
    assert.equal(completionByLabel.get("extends").kind, "Keyword");
    assert.equal(completionByLabel.get("delegate").kind, "Keyword");

    const grammar = readJson("syntaxes/foundation.tmLanguage.json");
    assert.match(grammar.repository.contractDefinitions.patterns[0].beginCaptures[6].name,
        /inheritance/);
    const snippets = readJson("snippets/foundation.json");
    assert.equal(snippets["Delegated contract implementation"].prefix, "delegate");
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
        contract Reader { fn Read(self) i32 fn hidden(self) i32 }
        struct Value implements Reader {
            fn Read(self) i32 { 1 }
            fn hidden(self) i32 { 2 }
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
    assert.deepEqual(snippets["Result binding without error"].body, [
        "const ${1:value} = ${2:result} else {",
        "    ${3:return}",
        "}"
    ]);
    assert.deepEqual(snippets["Result expression without error"].body, [
        "${1:result} else {",
        "    ${2:return}",
        "}"
    ]);
    assert.match(readJson("syntaxes/foundation.tmLanguage.json").repository.keywords.patterns[3].match,
        /else/);
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
    assert.equal(snippets["DI service"].prefix, "diservice");
    assert.equal(snippets["Build generated application"].prefix, "apphost");
    assert.equal(snippets.Action.prefix, "action");
    assert.match(snippets.Action.body[0], /\(self,/);
    assert.equal(snippets.Task.prefix, "task");
    assert.equal(snippets["Spawn and wait"].prefix, "spawnwait");
    assert.equal(snippets["Channel endpoints"].prefix, "channel");
    assert.equal(snippets["Cancellation source"].prefix, "cancellation");
    assert.equal(snippets["Task supervisor"].prefix, "supervisor");
    assert.equal(snippets["Parallel task pool"].prefix, "workerpool");
    assert.equal(snippets["TCP client tasks"].prefix, "tcpclient");
    assert.equal(snippets["State machine"].prefix, "statemachine");
    assert.equal(snippets["Channel select"].prefix, "select");
    assert.equal(snippets.Pipeline.prefix, "pipeline");
    assert.equal(snippets.Saga.prefix, "saga");
    assert.equal(snippets.Test.prefix, "test");
    assert.equal(snippets["Unsafe block"].prefix, "unsafe");
    assert.equal(snippets["Indexed for loop"].prefix, "forindex");
    assert.equal(snippets["Editable for loop"].prefix, "foredit");
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
            for index, item in [1, 2] {
                discard item
            }
            0
        }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("user").detail, "Local binding");
    assert.equal(byLabel.get("index").detail, "Loop binding");
    assert.equal(byLabel.get("item").detail, "Loop binding");
    assert.equal(byLabel.get("displayName").detail, "Distributed method of User");
    assert.equal(byLabel.get("rename").detail, "Distributed method of User");
});

test("tracks task declarations and owned waits", () => {
    const completions = collectCompletions(`
        task fetchUser(id i32) i32 {
            id
        }

        fn main() i32 {
            const pending = spawn fetchUser(1)
            const user = $pending.wait()
            user
        }
    `);
    const labels = new Set(completions.map((entry) => entry.label));
    assert.ok(labels.has("fetchUser"));
    assert.ok(labels.has("pending"));
    assert.ok(labels.has("user"));
    assert.ok(labels.has("Task"));
});

test("collects declarations that use arrays and slices", () => {
    const completions = collectCompletions(`
        struct Batch { names [2]String }
        fn first(names [String], positions [2]i32) void {
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
            const result = left + right
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
            const text = "fn hidden_in_string(value i32)"
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
    const source = "fn same(same i32) i32 { const same = same return same }";
    const first = collectCompletions(source);
    const second = collectCompletions(source);

    assert.deepEqual(first, second);
    assert.equal(
        first.filter((entry) => entry.label === "same" && entry.kind === "Variable").length,
        1
    );
});
