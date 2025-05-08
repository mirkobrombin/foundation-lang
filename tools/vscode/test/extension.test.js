"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const {
    collectCompletions,
    findHover,
    maskTrivia,
    staticCompletions
} = require("../src/completions");

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
    assert.match(packagingScript, /package\.json/);
    assert.match(packagingScript, /foundation-lang-\$version\.vsix/);
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
        "implements",
        "fn",
        "let",
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
    assert.ok(completionLabels.has("c"));
    assert.ok(completionLabels.has("@target(...)"));
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
    const cAbiDeclaration = parsedGrammar.repository.cAbiDeclarations.patterns[0];
    assert.equal(cAbiDeclaration.name, "meta.function.external.foundation");
    assert.match(cAbiDeclaration.begin, /extern/);
    assert.ok(cAbiDeclaration.patterns.some((pattern) =>
        pattern.captures?.[2]?.name === "entity.name.function.external.foundation"
    ));
    for (const sequence of ["[N]T", "view [T]", "edit [T]"]) {
        assert.ok(completionLabels.has(sequence));
    }
    assert.ok(completionLabels.has("fn(...) R"));
    const snippets = readJson("snippets/foundation.json");
    assert.match(snippets["Target declaration"].body.join("\n"), /@target/);
    assert.match(snippets["Read environment value"].body.join("\n"), /env\.Get\(view/);
    assert.match(snippets["Join path"].body, /path\.Join\(view/);
    assert.match(snippets["Open line reader"].body.join("\n"), /fs\.OpenLines\(view/);
    assert.match(snippets["Parse JSON value"].body.join("\n"), /json\.Parse\(view/);
    assert.match(snippets["String builder"].body.join("\n"), /text\.NewBuilder/);
    assert.match(snippets["Format UTC time"].body.join("\n"), /FormatUtc/);
    assert.match(snippets["Main function with arguments"].body.join("\n"),
        /main\(args view \[String\]\) i32/);
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
    assert.equal(
        grammar.repository.structDefinitions.patterns[0].patterns[1].include,
        "#blocks"
    );
    assert.equal(grammar.repository.blocks.patterns[0].patterns[0].include, "$self");
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

    assert.equal(snippets["Result binding"].prefix, "letelse");
    assert.equal(snippets["Discard value"].prefix, "discard");
    assert.equal(snippets.Panic.prefix, "panic");
    assert.equal(snippets["Owned value"].prefix, "own");
    assert.equal(snippets["View parameter"].prefix, "view");
    assert.equal(snippets["Edit parameter"].prefix, "edit");
    assert.equal(snippets["Fixed array binding"].prefix, "array");
    assert.equal(snippets["View slice parameter"].prefix, "viewslice");
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
