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
permissions. Symbolic links are never replaced. Project traversal reads `.fdn` files recursively
and skips hidden directories and directories named `build`.

Stdout mode accepts one `.fdn` file. Invalid command input returns status 2. Syntax, read, or write
failure returns status 1 and prints a diagnostic.

## Editor

`foundation-ls` exposes document and range formatting. Document formatting returns one complete
edit. Range formatting computes indentation from the complete unsaved buffer, then returns edits
only for selected lines. Editor tab settings do not change canonical Foundation output.
