# Linter

`foundationc lint <source-or-project>` validates the complete compiler graph and then checks root
project source against the selected Foundation Code Standard profile. It does not require a `main`
function, so the command works for reusable packages.

The optional `fcs` directive in `foundation.package` selects `valid`, `standard`, or `strict`.
Standard is the default when the directive or a manifest is absent. A command-line override applies
only to that invocation:

```sh
foundationc lint source
foundationc lint source --profile strict
```

The command writes deterministic compiler diagnostics and lint warnings to standard error. It
returns 0 when no finding exists, 1 for compiler errors or lint findings, and 2 for invalid command
arguments. Valid still performs parsing, type checking, ownership analysis, package validation, and
all other compiler checks; it disables only source-style findings.

Stage 0 enforces these rules:

- `FCS1001`: Standard source stays within 100 columns and Strict source stays within 80 columns.
- `FCS2001`: every exported API symbol has attached `//` documentation under Strict.

Width uses four-column tab stops and counts each UTF-8 scalar start as one display column. Root test
sources are checked with the root production sources. Locked dependencies, SDK packages, framework
packages, and compiler-generated sources are excluded. Diagnostics use their original path, line,
column, and source span.

The language server runs the same engine after successful semantic analysis and publishes lint
findings with warning severity. Changing `foundation.package` invalidates workspace analysis, so an
`fcs` profile change is reflected without restarting the server.

The complete accepted policy and the stage 0 enforcement table are in
[foundation-code-standard.md](foundation-code-standard.md).
