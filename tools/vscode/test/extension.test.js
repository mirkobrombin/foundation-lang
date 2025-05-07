"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const { collectCompletions, maskTrivia, staticCompletions } = require("../src/completions");

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
        "struct",
        "enum",
        "fn",
        "let",
        "var",
        "return",
        "discard",
        "if",
        "else",
        "while",
        "match",
        "true",
        "false"
    ]);
    for (const keyword of compilerKeywords) {
        assert.match(grammar, new RegExp(`\\b${keyword}\\b`));
        assert.ok(completionLabels.has(keyword));
    }

    for (const type of ["i32", "bool", "String", "void", "Option", "Result", "print", "panic"]) {
        assert.match(grammar, new RegExp(`\\b${type}\\b`));
        assert.ok(completionLabels.has(type));
    }
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
});

test("ships Result handling and panic snippets", () => {
    const snippets = readJson("snippets/foundation.json");

    assert.equal(snippets["Result binding"].prefix, "letelse");
    assert.equal(snippets["Discard value"].prefix, "discard");
    assert.equal(snippets.Panic.prefix, "panic");
});

test("collects structs and their fields", () => {
    const completions = collectCompletions(`
        struct User {
            id i32
            name String
        }
        fn main() i32 { 0 }
    `);
    const byLabel = new Map(completions.map((entry) => [entry.label, entry]));

    assert.equal(byLabel.get("User").kind, "Struct");
    assert.equal(byLabel.get("User").insertText, "User { id = ${1:id} name = ${2:name} }");
    assert.equal(byLabel.get("id").detail, "Field of User");
    assert.equal(byLabel.get("name").detail, "Field of User");
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
    assert.ok(!labels.includes("value"));
});

test("masks trivia without changing source offsets", () => {
    const source = "fn main() void {\n    print(\"escaped \\\" text\") // note\n}\n";
    const masked = maskTrivia(source);

    assert.equal(masked.length, source.length);
    assert.equal(masked.split("\n").length, source.split("\n").length);
    assert.match(masked, /fn main/);
    assert.doesNotMatch(masked, /escaped|note/);
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
