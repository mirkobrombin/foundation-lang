# Source formatting

The Foundation formatter produces stable, idempotent source while retaining layout-sensitive line
boundaries. It uses compiler tokens, the AST, and parser lookahead instead of maintaining a second
grammar. It preserves token kinds, decoded token values, comments, and every syntax-significant
line boundary. Line endings outside string literals become LF, indentation uses four spaces,
trailing whitespace is removed, and spaces around punctuation and operators follow the language
grammar.

Formatting requires syntactically valid source. Lexer or parser diagnostics leave the input
unchanged. The formatter scans its output again and rejects any rewrite that changes the token
stream or produces invalid syntax.

## CLI

`foundationc format <source>` writes one formatted file to stdout.

`foundationc format --check <source-or-project>` writes each unformatted path in deterministic
order and exits with status 1 when changes are needed. It writes nothing and exits with status 0
when every file is formatted.

`foundationc format --write <source-or-project>` validates the complete input before writing.
Each changed file is replaced through a temporary file in the same directory while preserving its
permissions. Symbolic links are never replaced. Project traversal reads `.fn` files recursively
and skips hidden directories and directories named `build`.

Stdout mode accepts one `.fn` file. Invalid command input returns status 2. Syntax, read, or write
failure returns status 1 and prints a diagnostic.

## Editor

`foundation-ls` exposes document and range formatting. Document formatting organizes imports,
formats the complete unsaved buffer, and returns one edit. The server also implements
`textDocument/willSaveWaitUntil`, so any LSP client can apply the same operation before saving.
Range formatting computes indentation from the complete buffer and returns edits only for selected
lines. It does not change imports. Editor tab settings do not change canonical Foundation output.

## Imports

`foundationc imports <source>` writes one source file with canonical imports to stdout.
`foundationc imports --check <source-or-project>` reports files that need changes, and
`foundationc imports --write <source-or-project>` updates them.

Import organization sorts imports by package and alias, removes aliases unused by the file, and
adds a missing import when one package provides an exact alias and exported member match. Candidate
packages come from the SDK and the dependency graph already recorded for the project. An ambiguous
match remains a diagnostic.

The command and language server do not fetch packages or edit `foundation.package` and
`foundation.lock`. Adding a dependency remains an explicit package operation. No background daemon
is required: the CLI and each LSP client call the compiler-owned operation directly.
