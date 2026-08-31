"use strict";

const childProcess = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");

const maxMessageBytes = 16 * 1024 * 1024;

function encodeMessage(message) {
    const body = Buffer.from(JSON.stringify(message), "utf8");
    return Buffer.concat([
        Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, "ascii"),
        body
    ]);
}

class MessageReader {
    constructor(onMessage, onError) {
        this.buffer = Buffer.alloc(0);
        this.onMessage = onMessage;
        this.onError = onError;
    }

    append(chunk) {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        while (this.readOne()) {}
    }

    readOne() {
        const separator = this.buffer.indexOf("\r\n\r\n");
        if (separator < 0) {
            return false;
        }
        const header = this.buffer.subarray(0, separator).toString("ascii");
        const lengths = [...header.matchAll(/^Content-Length:\s*([0-9]+)\s*$/gmi)];
        if (lengths.length !== 1) {
            this.onError(new Error("Invalid language server message header"));
            this.buffer = Buffer.alloc(0);
            return false;
        }
        const length = Number(lengths[0][1]);
        if (!Number.isSafeInteger(length) || length > maxMessageBytes) {
            this.onError(new Error("Language server message is too large"));
            this.buffer = Buffer.alloc(0);
            return false;
        }
        const bodyStart = separator + 4;
        if (this.buffer.length < bodyStart + length) {
            return false;
        }
        const body = this.buffer.subarray(bodyStart, bodyStart + length).toString("utf8");
        this.buffer = this.buffer.subarray(bodyStart + length);
        try {
            this.onMessage(JSON.parse(body));
        } catch (error) {
            this.onError(error);
        }
        return true;
    }
}

function executableName() {
    return process.platform === "win32" ? "foundation-ls.exe" : "foundation-ls";
}

function platformDirectory() {
    return `${process.platform}-${process.arch}`;
}

function findLanguageServer(vscode, extensionPath) {
    const configured = vscode.workspace
        .getConfiguration("foundation")
        .get("languageServer.path", "");
    if (configured) {
        return configured;
    }

    const name = executableName();
    const bundled = path.join(extensionPath, "bin", platformDirectory(), name);
    if (fs.existsSync(bundled)) {
        return bundled;
    }

    const legacyBundled = path.join(extensionPath, "bin", name);
    if (fs.existsSync(legacyBundled)) {
        return legacyBundled;
    }

    for (const folder of vscode.workspace.workspaceFolders || []) {
        let current = folder.uri.fsPath;
        while (true) {
            for (const configuration of ["dev", "release", "clang"]) {
                const candidate = path.join(current, "build", configuration, name);
                if (fs.existsSync(candidate)) {
                    return candidate;
                }
            }
            const parent = path.dirname(current);
            if (parent === current) {
                break;
            }
            current = parent;
        }
    }
    return name;
}

function documentParams(document) {
    return {
        uri: document.uri.toString(),
        languageId: "foundation",
        version: document.version,
        text: document.getText()
    };
}

class FoundationLanguageClient {
    constructor(vscode, context) {
        this.vscode = vscode;
        this.context = context;
        this.process = undefined;
        this.reader = undefined;
        this.nextId = 1;
        this.pending = new Map();
        this.ready = false;
        this.stopping = false;
        this.output = vscode.window.createOutputChannel("Foundation Language Server");
        this.diagnostics = vscode.languages.createDiagnosticCollection("foundation");
        this.status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
        this.status.name = "Foundation IntelliSense";
        this.status.command = "foundation.showOutput";
        this.setStatus("starting");
        context.subscriptions.push(
            this.output,
            this.diagnostics,
            this.status,
            vscode.commands.registerCommand("foundation.showOutput", () => this.output.show(true))
        );
    }

    setStatus(state, detail = "") {
        const values = {
            starting: ["$(sync~spin) Foundation: Starting", "Foundation IntelliSense is starting"],
            ready: ["$(check) Foundation: Ready", "Foundation IntelliSense is ready"],
            error: ["$(error) Foundation: Error", "Foundation IntelliSense failed"],
            stopped: ["$(circle-slash) Foundation: Stopped", "Foundation IntelliSense stopped"]
        };
        const [text, tooltip] = values[state] || values.error;
        this.status.text = text;
        this.status.tooltip = detail ? `${tooltip}\n${detail}` : tooltip;
        this.status.show();
    }

    async start() {
        const executable = findLanguageServer(this.vscode, this.context.extensionPath);
        this.setStatus("starting", executable);
        this.output.appendLine(`Starting ${executable}`);
        const folder = this.vscode.workspace.workspaceFolders?.[0];
        const options = folder ? { cwd: folder.uri.fsPath } : {};
        this.process = childProcess.spawn(executable, [], {
            ...options,
            stdio: ["pipe", "pipe", "pipe"],
            windowsHide: true
        });
        this.reader = new MessageReader(
            (message) => this.handleMessage(message),
            (error) => this.fail(error)
        );
        this.process.stdout.on("data", (chunk) => this.reader.append(chunk));
        this.process.stderr.on("data", (chunk) => this.output.append(chunk.toString("utf8")));
        this.process.on("error", (error) => this.fail(error));
        this.process.on("exit", (code, signal) => {
            if (!this.stopping) {
                this.fail(new Error(`foundation-ls exited with code ${code}, signal ${signal}`));
            }
        });

        const rootUri = folder?.uri.toString() || null;
        await this.request("initialize", {
            processId: process.pid,
            rootUri,
            capabilities: {
                general: { positionEncodings: ["utf-16"] },
                textDocument: {
                    publishDiagnostics: { relatedInformation: true },
                    synchronization: { willSaveWaitUntil: true }
                }
            },
            workspaceFolders: (this.vscode.workspace.workspaceFolders || []).map((item) => ({
                uri: item.uri.toString(),
                name: item.name
            }))
        });
        this.notify("initialized", {});
        this.output.appendLine("Server initialized");

        const semanticLegend = new this.vscode.SemanticTokensLegend([
            "function", "method", "struct", "property", "enum", "enumMember",
            "interface", "decorator", "parameter", "variable"
        ], ["declaration"]);
        const sourceWatcher = this.vscode.workspace.createFileSystemWatcher("**/*.fn");
        const manifestWatcher = this.vscode.workspace.createFileSystemWatcher(
            "**/foundation.package"
        );
        const lockWatcher = this.vscode.workspace.createFileSystemWatcher("**/foundation.lock");
        const watchedFile = (type) => (uri) => {
            this.notify("workspace/didChangeWatchedFiles", {
                changes: [{ uri: uri.toString(), type }]
            });
        };

        this.context.subscriptions.push(
            sourceWatcher,
            manifestWatcher,
            lockWatcher,
            sourceWatcher.onDidCreate(watchedFile(1)),
            sourceWatcher.onDidChange(watchedFile(2)),
            sourceWatcher.onDidDelete(watchedFile(3)),
            manifestWatcher.onDidCreate(watchedFile(1)),
            manifestWatcher.onDidChange(watchedFile(2)),
            manifestWatcher.onDidDelete(watchedFile(3)),
            lockWatcher.onDidCreate(watchedFile(1)),
            lockWatcher.onDidChange(watchedFile(2)),
            lockWatcher.onDidDelete(watchedFile(3)),
            this.vscode.workspace.onDidOpenTextDocument((document) => {
                this.open(document);
            }),
            this.vscode.workspace.onDidChangeTextDocument((event) => {
                this.change(event.document);
            }),
            this.vscode.workspace.onWillSaveTextDocument((event) => {
                if (!this.ready || event.document.languageId !== "foundation") {
                    return;
                }
                event.waitUntil(this.willSave(event.document, event.reason).catch((error) => {
                    this.output.appendLine(`Save formatting failed: ${error.message}`);
                    return [];
                }));
            }),
            this.vscode.workspace.onDidCloseTextDocument((document) => this.close(document)),
            this.vscode.workspace.onDidChangeWorkspaceFolders((event) => {
                const folder = (item) => ({ uri: item.uri.toString(), name: item.name });
                this.notify("workspace/didChangeWorkspaceFolders", {
                    event: {
                        added: event.added.map(folder),
                        removed: event.removed.map(folder)
                    }
                });
            }),
            this.vscode.workspace.registerTextDocumentContentProvider("foundation-builtin", {
                provideTextDocumentContent: (uri) => this.builtinDocument(uri)
            }),
            this.vscode.languages.registerDocumentSymbolProvider("foundation", {
                provideDocumentSymbols: (document, token) => this.documentSymbols(document, token)
            }),
            this.vscode.languages.registerHoverProvider("foundation", {
                provideHover: (document, position, token) => this.hover(document, position, token)
            }),
            this.vscode.languages.registerDeclarationProvider("foundation", {
                provideDeclaration: (document, position, token) =>
                    this.declaration(document, position, token)
            }),
            this.vscode.languages.registerDefinitionProvider("foundation", {
                provideDefinition: (document, position, token) =>
                    this.definition(document, position, token)
            }),
            this.vscode.languages.registerTypeDefinitionProvider("foundation", {
                provideTypeDefinition: (document, position, token) =>
                    this.typeDefinition(document, position, token)
            }),
            this.vscode.languages.registerImplementationProvider("foundation", {
                provideImplementation: (document, position, token) =>
                    this.implementations(document, position, token)
            }),
            this.vscode.languages.registerDocumentHighlightProvider("foundation", {
                provideDocumentHighlights: (document, position, token) =>
                    this.documentHighlights(document, position, token)
            }),
            this.vscode.languages.registerCodeLensProvider("foundation", {
                provideCodeLenses: (document, token) => this.codeLenses(document, token)
            }),
            this.vscode.languages.registerTypeHierarchyProvider("foundation", {
                prepareTypeHierarchy: (document, position, token) =>
                    this.prepareTypeHierarchy(document, position, token),
                provideTypeHierarchySupertypes: (item, token) =>
                    this.typeHierarchySupertypes(item, token),
                provideTypeHierarchySubtypes: (item, token) =>
                    this.typeHierarchySubtypes(item, token)
            }),
            this.vscode.languages.registerCallHierarchyProvider("foundation", {
                prepareCallHierarchy: (document, position, token) =>
                    this.prepareCallHierarchy(document, position, token),
                provideCallHierarchyIncomingCalls: (item, token) =>
                    this.callHierarchyIncomingCalls(item, token),
                provideCallHierarchyOutgoingCalls: (item, token) =>
                    this.callHierarchyOutgoingCalls(item, token)
            }),
            this.vscode.languages.registerReferenceProvider("foundation", {
                provideReferences: (document, position, context, token) =>
                    this.references(document, position, context, token)
            }),
            this.vscode.languages.registerRenameProvider("foundation", {
                prepareRename: (document, position, token) =>
                    this.prepareRename(document, position, token),
                provideRenameEdits: (document, position, newName, token) =>
                    this.rename(document, position, newName, token)
            }),
            this.vscode.languages.registerCompletionItemProvider("foundation", {
                provideCompletionItems: (document, position, token) =>
                    this.completions(document, position, token)
            }, ".", "@"),
            this.vscode.languages.registerSignatureHelpProvider("foundation", {
                provideSignatureHelp: (document, position, token) =>
                    this.signatureHelp(document, position, token)
            }, "(", ","),
            this.vscode.languages.registerDocumentSemanticTokensProvider("foundation", {
                provideDocumentSemanticTokens: (document, token) =>
                    this.semanticTokens(document, token)
            }, semanticLegend),
            this.vscode.languages.registerInlayHintsProvider("foundation", {
                provideInlayHints: (document, range, token) =>
                    this.inlayHints(document, range, token)
            }),
            this.vscode.languages.registerCodeActionsProvider("foundation", {
                provideCodeActions: (document, range, context, token) =>
                    this.codeActions(document, range, context, token)
            }, {
                providedCodeActionKinds: [
                    this.vscode.CodeActionKind.QuickFix,
                    this.vscode.CodeActionKind.SourceOrganizeImports
                ]
            }),
            this.vscode.languages.registerFoldingRangeProvider("foundation", {
                provideFoldingRanges: (document, context, token) =>
                    this.foldingRanges(document, token)
            }),
            this.vscode.languages.registerSelectionRangeProvider("foundation", {
                provideSelectionRanges: (document, positions, token) =>
                    this.selectionRanges(document, positions, token)
            }),
            this.vscode.languages.registerDocumentFormattingEditProvider("foundation", {
                provideDocumentFormattingEdits: (document, options, token) =>
                    this.formatting(document, options, token)
            }),
            this.vscode.languages.registerDocumentRangeFormattingEditProvider("foundation", {
                provideDocumentRangeFormattingEdits: (document, range, options, token) =>
                    this.rangeFormatting(document, range, options, token)
            }),
            this.vscode.languages.registerWorkspaceSymbolProvider({
                provideWorkspaceSymbols: (query, token) => this.workspaceSymbols(query, token)
            }),
            this.vscode.commands.registerCommand(
                "foundation.openCompositeType",
                (uri, position) => this.openCompositeType(uri, position)
            ),
            this.vscode.commands.registerCommand(
                "foundation.openTypeDefinition",
                (uri, position) => this.openTypeDefinition(uri, position)
            )
        );
        this.ready = true;
        for (const document of this.vscode.workspace.textDocuments) {
            this.open(document);
        }
        this.setStatus("ready", executable);
        this.output.appendLine("IntelliSense providers registered");
    }

    open(document) {
        if (!this.ready || document.languageId !== "foundation") {
            return;
        }
        this.notify("textDocument/didOpen", { textDocument: documentParams(document) });
    }

    change(document) {
        if (!this.ready || document.languageId !== "foundation") {
            return;
        }
        this.notify("textDocument/didChange", {
            textDocument: { uri: document.uri.toString(), version: document.version },
            contentChanges: [{ text: document.getText() }]
        });
    }

    close(document) {
        if (!this.ready || document.languageId !== "foundation") {
            return;
        }
        this.notify("textDocument/didClose", {
            textDocument: { uri: document.uri.toString() }
        });
        this.diagnostics.delete(document.uri);
    }

    handleMessage(message) {
        if (Object.hasOwn(message, "id")) {
            const pending = this.pending.get(message.id);
            if (!pending) {
                return;
            }
            this.pending.delete(message.id);
            pending.cancellation?.dispose();
            if (message.error) {
                pending.reject(new Error(message.error.message || "Language server request failed"));
            } else {
                pending.resolve(message.result);
            }
            return;
        }
        if (message.method === "textDocument/publishDiagnostics") {
            this.publishDiagnostics(message.params);
        }
    }

    publishDiagnostics(params) {
        if (!params?.uri || !Array.isArray(params.diagnostics)) {
            return;
        }
        const uri = this.vscode.Uri.parse(params.uri);
        const diagnostics = params.diagnostics.map((entry) => {
            const range = new this.vscode.Range(
                entry.range.start.line,
                entry.range.start.character,
                entry.range.end.line,
                entry.range.end.character
            );
            const severity = entry.severity === 2
                ? this.vscode.DiagnosticSeverity.Warning
                : this.vscode.DiagnosticSeverity.Error;
            const diagnostic = new this.vscode.Diagnostic(range, entry.message, severity);
            diagnostic.source = entry.source || "foundation";
            diagnostic.code = entry.code;
            return diagnostic;
        });
        this.diagnostics.set(uri, diagnostics);
    }

    range(value) {
        return new this.vscode.Range(
            value.start.line,
            value.start.character,
            value.end.line,
            value.end.character
        );
    }

    documentSymbol(value) {
        const symbol = new this.vscode.DocumentSymbol(
            value.name,
            value.detail || "",
            Math.max(0, value.kind - 1),
            this.range(value.range),
            this.range(value.selectionRange)
        );
        symbol.children = (value.children || []).map((child) => this.documentSymbol(child));
        return symbol;
    }

    async documentSymbols(document, token) {
        const result = await this.request("textDocument/documentSymbol", {
            textDocument: { uri: document.uri.toString() }
        }, token);
        return (result || []).map((value) => this.documentSymbol(value));
    }

    async workspaceSymbols(query, token) {
        const result = await this.request("workspace/symbol", { query }, token);
        return (result || []).map((value) => {
            const location = new this.vscode.Location(
                this.vscode.Uri.parse(value.location.uri),
                this.range(value.location.range)
            );
            return new this.vscode.SymbolInformation(
                value.name,
                Math.max(0, value.kind - 1),
                value.containerName || "",
                location
            );
        });
    }

    trustMarkdown(contents, command) {
        const enabled = contents.isTrusted?.enabledCommands || [];
        contents.isTrusted = {
            enabledCommands: [...new Set([...enabled, command])]
        };
    }

    markdown(value, typeTargets = []) {
        const contents = new this.vscode.MarkdownString(value || "");
        if (typeTargets.length === 0) {
            return contents;
        }
        const links = typeTargets.map((target) => {
            const argumentsValue = encodeURIComponent(JSON.stringify([
                target.uri,
                target.position
            ]));
            return `[${target.label}]` +
                `(command:foundation.openTypeDefinition?${argumentsValue})`;
        });
        const separator = contents.value ? "\n\n" : "";
        const markdown = `${separator}**Types**: ${links.join(", ")}`;
        if (typeof contents.appendMarkdown === "function") {
            contents.appendMarkdown(markdown);
        } else {
            contents.value += markdown;
        }
        this.trustMarkdown(contents, "foundation.openTypeDefinition");
        return contents;
    }

    async hover(document, position, token) {
        const result = await this.request("textDocument/hover", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        }, token);
        if (!result) {
            return undefined;
        }
        const contents = this.markdown(
            result.contents?.value || "",
            result.foundationTypes || []
        );
        return new this.vscode.Hover(contents, this.range(result.range));
    }

    async definition(document, position, token) {
        const result = await this.request("textDocument/definition", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        }, token);
        return this.locations(result);
    }

    async declaration(document, position, token) {
        const result = await this.request("textDocument/declaration", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        }, token);
        return this.locations(result);
    }

    async typeDefinition(document, position, token) {
        const result = await this.request("textDocument/typeDefinition", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        }, token);
        return this.locations(result);
    }

    locations(result) {
        if (!result) {
            return undefined;
        }
        const values = Array.isArray(result) ? result : [result];
        return values.map((value) => new this.vscode.Location(
            this.vscode.Uri.parse(value.uri),
            this.range(value.range)
        ));
    }

    async implementations(document, position, token) {
        const result = await this.request("textDocument/implementation", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        }, token);
        return this.locations(result) || [];
    }

    async documentHighlights(document, position, token) {
        const result = await this.request("textDocument/documentHighlight", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        }, token);
        return (result || []).map((value) => new this.vscode.DocumentHighlight(
            this.range(value.range),
            value.kind
        ));
    }

    async codeLenses(document, token) {
        const result = await this.request("textDocument/codeLens", {
            textDocument: { uri: document.uri.toString() }
        }, token);
        return (result || []).map((value) => {
            if (value.command.command === "foundation.openCompositeType") {
                const [uri, position] = value.command.arguments;
                return new this.vscode.CodeLens(this.range(value.range), {
                    title: value.command.title,
                    command: value.command.command,
                    arguments: [
                        this.vscode.Uri.parse(uri),
                        new this.vscode.Position(position.line, position.character)
                    ]
                });
            }
            const [uri, position, locations] = value.command.arguments;
            const command = {
                title: value.command.title,
                command: value.command.command,
                arguments: [
                    this.vscode.Uri.parse(uri),
                    new this.vscode.Position(position.line, position.character),
                    locations.map((location) => new this.vscode.Location(
                        this.vscode.Uri.parse(location.uri),
                        this.range(location.range)
                    ))
                ]
            };
            return new this.vscode.CodeLens(this.range(value.range), command);
        });
    }

    async requestCompositeType(sourceUri, position, typeName, packageName) {
        const params = typeName
            ? { sourceUri, typeName, packageName }
            : {
                textDocument: { uri: sourceUri },
                position: { line: position.line, character: position.character }
            };
        return this.request("foundation/compositeType", params);
    }

    async openCompositeType(uriValue, positionValue) {
        const sourceUri = typeof uriValue === "string" ? uriValue : uriValue.toString();
        const position = positionValue || { line: 0, character: 0 };
        const value = await this.requestCompositeType(sourceUri, position);
        if (!value) {
            this.vscode.window.showErrorMessage(
                "Foundation could not locate this type in the current project"
            );
            return;
        }
        const locations = value.fragments
            .filter((fragment) => fragment.kind !== "struct" || fragment.key === "struct-prefix")
            .map((fragment) => new this.vscode.Location(
                this.vscode.Uri.parse(fragment.uri),
                this.range(fragment.range)
            ));
        if (locations.length === 0) {
            this.vscode.window.showErrorMessage(
                "Foundation found this type without any source locations"
            );
            return;
        }
        await this.vscode.commands.executeCommand(
            "editor.action.showReferences",
            this.vscode.Uri.parse(sourceUri),
            new this.vscode.Position(position.line, position.character),
            locations
        );
    }

    async openTypeDefinition(uriValue, positionValue) {
        const uri = this.vscode.Uri.parse(
            typeof uriValue === "string" ? uriValue : uriValue.toString()
        );
        const position = new this.vscode.Position(
            positionValue.line,
            positionValue.character
        );
        const document = await this.vscode.workspace.openTextDocument(uri);
        const editor = await this.vscode.window.showTextDocument(document);
        editor.selection = new this.vscode.Selection(position, position);
        editor.revealRange(
            new this.vscode.Range(position, position),
            this.vscode.TextEditorRevealType.InCenterIfOutsideViewport
        );
    }

    typeHierarchyItem(value) {
        const item = new this.vscode.TypeHierarchyItem(
            Math.max(0, value.kind - 1),
            value.name,
            value.detail || "",
            this.vscode.Uri.parse(value.uri),
            this.range(value.range),
            this.range(value.selectionRange)
        );
        item.data = value.data;
        return item;
    }

    async prepareTypeHierarchy(document, position, token) {
        const result = await this.request("textDocument/prepareTypeHierarchy", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        }, token);
        return (result || []).map((value) => this.typeHierarchyItem(value));
    }

    async typeHierarchySupertypes(item, token) {
        const result = await this.request("typeHierarchy/supertypes", {
            item: { data: item.data }
        }, token);
        return (result || []).map((value) => this.typeHierarchyItem(value));
    }

    async typeHierarchySubtypes(item, token) {
        const result = await this.request("typeHierarchy/subtypes", {
            item: { data: item.data }
        }, token);
        return (result || []).map((value) => this.typeHierarchyItem(value));
    }

    callHierarchyItem(value) {
        const item = new this.vscode.CallHierarchyItem(
            Math.max(0, value.kind - 1),
            value.name,
            value.detail || "",
            this.vscode.Uri.parse(value.uri),
            this.range(value.range),
            this.range(value.selectionRange)
        );
        item.data = value.data;
        return item;
    }

    async prepareCallHierarchy(document, position, token) {
        const result = await this.request("textDocument/prepareCallHierarchy", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        }, token);
        return (result || []).map((value) => this.callHierarchyItem(value));
    }

    async callHierarchyIncomingCalls(item, token) {
        const result = await this.request("callHierarchy/incomingCalls", {
            item: { data: item.data }
        }, token);
        return (result || []).map((value) => new this.vscode.CallHierarchyIncomingCall(
            this.callHierarchyItem(value.from),
            value.fromRanges.map((range) => this.range(range))
        ));
    }

    async callHierarchyOutgoingCalls(item, token) {
        const result = await this.request("callHierarchy/outgoingCalls", {
            item: { data: item.data }
        }, token);
        return (result || []).map((value) => new this.vscode.CallHierarchyOutgoingCall(
            this.callHierarchyItem(value.to),
            value.fromRanges.map((range) => this.range(range))
        ));
    }

    async references(document, position, context, token) {
        const result = await this.request("textDocument/references", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character },
            context: { includeDeclaration: context.includeDeclaration }
        }, token);
        return (result || []).map((value) => new this.vscode.Location(
            this.vscode.Uri.parse(value.uri),
            this.range(value.range)
        ));
    }

    async prepareRename(document, position, token) {
        const result = await this.request("textDocument/prepareRename", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        }, token);
        if (!result) {
            throw new Error("This Foundation symbol cannot be renamed");
        }
        return { range: this.range(result.range), placeholder: result.placeholder };
    }

    async rename(document, position, newName, token) {
        const result = await this.request("textDocument/rename", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character },
            newName
        }, token);
        if (!result?.changes) {
            throw new Error("The requested Foundation rename is not safe");
        }
        const edit = new this.vscode.WorkspaceEdit();
        for (const [uri, edits] of Object.entries(result.changes)) {
            const target = this.vscode.Uri.parse(uri);
            for (const value of edits) {
                edit.replace(target, this.range(value.range), value.newText);
            }
        }
        return edit;
    }

    async completions(document, position, token) {
        const result = await this.request("textDocument/completion", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        }, token);
        return (result || []).map((value) => {
            const item = new this.vscode.CompletionItem(
                value.label,
                Math.max(0, value.kind - 1)
            );
            item.detail = value.detail;
            if (value.documentation) {
                item.documentation = new this.vscode.MarkdownString(
                    value.documentation.value || value.documentation
                );
            }
            if (value.insertText) {
                item.insertText = value.insertTextFormat === 2
                    ? new this.vscode.SnippetString(value.insertText)
                    : value.insertText;
            }
            if (value.command) {
                item.command = value.command;
            }
            return item;
        });
    }

    async builtinDocument(uri) {
        const filename = uri.path.split("/").pop() || "";
        const name = filename.endsWith(".fn") ? filename.slice(0, -3) : "";
        if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) {
            return "";
        }
        const result = await this.request("foundation/builtinDocument", { name });
        return typeof result?.contents === "string" ? result.contents : "";
    }

    async signatureHelp(document, position, token) {
        const result = await this.request("textDocument/signatureHelp", {
            textDocument: { uri: document.uri.toString() },
            position: { line: position.line, character: position.character }
        }, token);
        if (!result) {
            return undefined;
        }
        const help = new this.vscode.SignatureHelp();
        help.signatures = (result.signatures || []).map((value) => {
            const signature = new this.vscode.SignatureInformation(
                value.label,
                value.documentation || value.foundationTypes?.length
                    ? this.markdown(
                        value.documentation?.value || value.documentation || "",
                        value.foundationTypes || []
                    )
                    : undefined
            );
            signature.parameters = (value.parameters || []).map((parameter) =>
                new this.vscode.ParameterInformation(
                    parameter.label,
                    parameter.documentation || parameter.foundationTypes?.length
                        ? this.markdown(
                            parameter.documentation?.value ||
                                parameter.documentation || "",
                            parameter.foundationTypes || []
                        )
                        : undefined
                )
            );
            return signature;
        });
        help.activeSignature = result.activeSignature || 0;
        help.activeParameter = result.activeParameter || 0;
        return help;
    }

    async semanticTokens(document, token) {
        const result = await this.request("textDocument/semanticTokens/full", {
            textDocument: { uri: document.uri.toString() }
        }, token);
        return new this.vscode.SemanticTokens(new Uint32Array(result?.data || []));
    }

    async inlayHints(document, range, token) {
        const result = await this.request("textDocument/inlayHint", {
            textDocument: { uri: document.uri.toString() },
            range: {
                start: { line: range.start.line, character: range.start.character },
                end: { line: range.end.line, character: range.end.character }
            }
        }, token);
        const showEmptyTests = this.vscode.workspace
            .getConfiguration("foundation")
            .get("inlayHints.emptyTests", true);
        const hints = (result || [])
            .filter((value) => showEmptyTests || value.foundationKind !== "emptyTest")
            .map((value) => {
                const position = new this.vscode.Position(
                    value.position.line,
                    value.position.character
                );
                const hint = new this.vscode.InlayHint(position, value.label, value.kind);
                hint.paddingLeft = value.paddingLeft || false;
                hint.paddingRight = value.paddingRight || false;
                return hint;
            });
        return hints;
    }

    workspaceEdit(changes) {
        const edit = new this.vscode.WorkspaceEdit();
        for (const [uri, values] of Object.entries(changes || {})) {
            const target = this.vscode.Uri.parse(uri);
            for (const value of values) {
                edit.replace(target, this.range(value.range), value.newText);
            }
        }
        return edit;
    }

    async codeActions(document, range, context, token) {
        const result = await this.request("textDocument/codeAction", {
            textDocument: { uri: document.uri.toString() },
            range: {
                start: { line: range.start.line, character: range.start.character },
                end: { line: range.end.line, character: range.end.character }
            },
            context: {
                diagnostics: context.diagnostics.map((diagnostic) => ({
                    range: {
                        start: {
                            line: diagnostic.range.start.line,
                            character: diagnostic.range.start.character
                        },
                        end: {
                            line: diagnostic.range.end.line,
                            character: diagnostic.range.end.character
                        }
                    },
                    severity: diagnostic.severity,
                    code: diagnostic.code,
                    source: diagnostic.source,
                    message: diagnostic.message
                }))
            }
        }, token);
        return (result || []).map((value) => {
            const kind = value.kind === "source.organizeImports"
                ? this.vscode.CodeActionKind.SourceOrganizeImports
                : this.vscode.CodeActionKind.QuickFix;
            const action = new this.vscode.CodeAction(
                value.title,
                kind
            );
            action.isPreferred = value.isPreferred || false;
            action.edit = this.workspaceEdit(value.edit?.changes);
            return action;
        });
    }

    async foldingRanges(document, token) {
        const result = await this.request("textDocument/foldingRange", {
            textDocument: { uri: document.uri.toString() }
        }, token);
        return (result || []).map((value) => {
            const kind = value.kind === "imports"
                ? this.vscode.FoldingRangeKind.Imports
                : undefined;
            return new this.vscode.FoldingRange(value.startLine, value.endLine, kind);
        });
    }

    selectionRange(value) {
        const parent = value.parent ? this.selectionRange(value.parent) : undefined;
        return new this.vscode.SelectionRange(this.range(value.range), parent);
    }

    async selectionRanges(document, positions, token) {
        const result = await this.request("textDocument/selectionRange", {
            textDocument: { uri: document.uri.toString() },
            positions: positions.map((position) => ({
                line: position.line,
                character: position.character
            }))
        }, token);
        return (result || []).map((value) => this.selectionRange(value));
    }

    formattingEdits(values) {
        return (values || []).map((value) => new this.vscode.TextEdit(
            this.range(value.range),
            value.newText
        ));
    }

    async formatting(document, options, token) {
        const result = await this.request("textDocument/formatting", {
            textDocument: { uri: document.uri.toString() },
            options
        }, token);
        return this.formattingEdits(result);
    }

    async willSave(document, reason) {
        const result = await this.request("textDocument/willSaveWaitUntil", {
            textDocument: { uri: document.uri.toString() },
            reason
        });
        return this.formattingEdits(result);
    }

    async rangeFormatting(document, range, options, token) {
        const result = await this.request("textDocument/rangeFormatting", {
            textDocument: { uri: document.uri.toString() },
            range: {
                start: { line: range.start.line, character: range.start.character },
                end: { line: range.end.line, character: range.end.character }
            },
            options
        }, token);
        return this.formattingEdits(result);
    }

    request(method, params, token) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            if (token?.isCancellationRequested) {
                reject(new this.vscode.CancellationError());
                return;
            }
            let cancellation;
            if (token) {
                cancellation = token.onCancellationRequested(() => {
                    const pending = this.pending.get(id);
                    if (!pending) {
                        return;
                    }
                    this.pending.delete(id);
                    this.notify("$/cancelRequest", { id });
                    pending.reject(new this.vscode.CancellationError());
                    pending.cancellation?.dispose();
                });
            }
            this.pending.set(id, { resolve, reject, cancellation });
            this.send({ jsonrpc: "2.0", id, method, params });
        });
    }

    notify(method, params) {
        this.send({ jsonrpc: "2.0", method, params });
    }

    send(message) {
        if (!this.process?.stdin.destroyed) {
            this.process.stdin.write(encodeMessage(message));
        }
    }

    fail(error) {
        this.ready = false;
        this.output.appendLine(error.message);
        this.setStatus("error", error.message);
        this.output.show(true);
        for (const pending of this.pending.values()) {
            pending.cancellation?.dispose();
            pending.reject(error);
        }
        this.pending.clear();
    }

    async stop() {
        if (!this.process || this.stopping) {
            return;
        }
        this.stopping = true;
        try {
            await this.request("shutdown", null);
        } catch (error) {
            this.output.appendLine(error.message);
        }
        this.notify("exit", null);
        this.setStatus("stopped");
        const process = this.process;
        setTimeout(() => {
            if (process.exitCode === null) {
                process.kill();
            }
        }, 1000).unref();
    }
}

module.exports = {
    FoundationLanguageClient,
    MessageReader,
    encodeMessage,
    findLanguageServer,
    platformDirectory
};
