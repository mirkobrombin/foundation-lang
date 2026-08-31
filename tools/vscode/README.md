# Foundation Lang for VS Code

The extension connects VS Code to `foundation-ls`, the language server built from the same semantic
model as `foundationc`. It reads the whole Foundation project, including packages and
compiler-derived application members, so editor features do not depend on generated source files.

## In the editor

- Live diagnostics cover syntax, types, ownership, attributes, package graphs, and Foundation
  application declarations.
- Completion, hover, signature help, and parameter documentation understand both user code and the
  standard packages shipped with the compiler.
- Go to Definition, Find References, Rename, Call Hierarchy, Type Hierarchy, and Go to
  Implementations use resolved symbols rather than matching names as text.
- Saving a Foundation file formats it, sorts and removes imports, and adds a missing import when the
  SDK or locked dependency graph contains one exact match.
- Semantic highlighting, parameter hints, and quick fixes follow the compiler's view of the open
  document.
- Composite Type Peek collects a struct and its distributed methods by source file while keeping
  every entry editable in its original file.
- `foundation.package` and `foundation.lock` have their own syntax, completion, hover, diagnostics,
  and formatting support.

The extension also supplies snippets for common ownership, task, channel, service, action, web,
interop, and testing forms. Snippets follow accepted compiler syntax; they are conveniences rather
than a second definition of the language.

## Language server

A packaged VSIX includes `foundation-ls` for its target platform. During extension development,
VS Code also looks for the server in workspace build directories and on `PATH`. Set
`foundation.languageServer.path` when a project should use a specific executable.

The Foundation status item reports when IntelliSense is starting, ready, stopped, or failed.
Selecting it opens the language server output, which is the first place to check when a project is
not being indexed.

Save handling is implemented by `foundation-ls`, not by a VS Code-only formatter. Other editors can
use the standard `textDocument/willSaveWaitUntil` request, and command-line workflows can run
`foundationc imports --write` and `foundationc format --write`.

The setting `foundation.inlayHints.emptyTests` controls the `is empty` hint shown for typed
`!value` checks without disabling parameter-name hints.

## Development

Run the dependency-free test suite:

```sh
node --test test/extension.test.js
```

Build the local VSIX:

```sh
scripts/package.sh
```

Cross-package a server built elsewhere by setting `FOUNDATION_VSCODE_PLATFORM` and
`FOUNDATION_LANGUAGE_SERVER` before running the script.
