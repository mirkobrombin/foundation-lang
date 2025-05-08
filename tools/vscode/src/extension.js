"use strict";

const vscode = require("vscode");
const { collectCompletions } = require("./completions");

const completionKinds = {
    Constant: vscode.CompletionItemKind.Constant,
    Enum: vscode.CompletionItemKind.Enum,
    EnumMember: vscode.CompletionItemKind.EnumMember,
    Field: vscode.CompletionItemKind.Field,
    Function: vscode.CompletionItemKind.Function,
    Keyword: vscode.CompletionItemKind.Keyword,
    Module: vscode.CompletionItemKind.Module,
    Struct: vscode.CompletionItemKind.Struct,
    TypeParameter: vscode.CompletionItemKind.TypeParameter,
    Variable: vscode.CompletionItemKind.Variable
};

function activate(context) {
    const provider = vscode.languages.registerCompletionItemProvider("foundation", {
        async provideCompletionItems(document) {
            const files = await vscode.workspace.findFiles("**/*.fdn", "**/{.git,build}/**", 500);
            const sources = await Promise.all(files.map(async (file) => {
                if (file.toString() === document.uri.toString()) {
                    return document.getText();
                }
                const contents = await vscode.workspace.fs.readFile(file);
                return Buffer.from(contents).toString("utf8");
            }));
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

    context.subscriptions.push(provider);
}

function deactivate() {}

module.exports = { activate, deactivate };
