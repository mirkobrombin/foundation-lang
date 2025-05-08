"use strict";

const vscode = require("vscode");
const { collectCompletions, findHover } = require("./completions");

const completionKinds = {
    Constant: vscode.CompletionItemKind.Constant,
    Contract: vscode.CompletionItemKind.Interface,
    Enum: vscode.CompletionItemKind.Enum,
    EnumMember: vscode.CompletionItemKind.EnumMember,
    Field: vscode.CompletionItemKind.Field,
    Function: vscode.CompletionItemKind.Function,
    Keyword: vscode.CompletionItemKind.Keyword,
    Module: vscode.CompletionItemKind.Module,
    Method: vscode.CompletionItemKind.Method,
    Struct: vscode.CompletionItemKind.Struct,
    TypeParameter: vscode.CompletionItemKind.TypeParameter,
    Variable: vscode.CompletionItemKind.Variable
};

function activate(context) {
    async function projectSources(document) {
        const files = await vscode.workspace.findFiles("**/*.fdn", "**/{.git,build}/**", 500);
        return Promise.all(files.map(async (file) => {
            if (file.toString() === document.uri.toString()) {
                return document.getText();
            }
            const contents = await vscode.workspace.fs.readFile(file);
            return Buffer.from(contents).toString("utf8");
        }));
    }

    const provider = vscode.languages.registerCompletionItemProvider("foundation", {
        async provideCompletionItems(document) {
            const sources = await projectSources(document);
            return collectCompletions(document.getText(), sources).map((entry) => {
                const item = new vscode.CompletionItem(entry.label, completionKinds[entry.kind]);
                item.detail = entry.detail;
                if (entry.insertText) {
                    item.insertText = new vscode.SnippetString(entry.insertText);
                }
                return item;
            });
        }
    }, ".");

    const hoverProvider = vscode.languages.registerHoverProvider("foundation", {
        async provideHover(document, position) {
            const range = document.getWordRangeAtPosition(position);
            if (!range) {
                return undefined;
            }
            const word = document.getText(range);
            const entry = findHover(document.getText(), word, await projectSources(document));
            if (!entry) {
                return undefined;
            }
            const contents = new vscode.MarkdownString();
            contents.appendCodeblock(entry.detail || entry.label, "foundation");
            return new vscode.Hover(contents, range);
        }
    });

    context.subscriptions.push(provider, hoverProvider);
}

function deactivate() {}

module.exports = { activate, deactivate };
