"use strict";

const vscode = require("vscode");
const { FoundationLanguageClient } = require("./languageClient");

let languageClient;

function activate(context) {
    languageClient = new FoundationLanguageClient(vscode, context);
    languageClient.start().catch((error) => {
        vscode.window.showErrorMessage(`Foundation language server: ${error.message}`);
    });
}

function deactivate() {
    return languageClient?.stop();
}

module.exports = { activate, deactivate };
