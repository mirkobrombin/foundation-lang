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
foundationc lint source --rule FCS1001=error
```

The command writes deterministic compiler diagnostics and lint findings to standard error. It
returns 0 when no finding exists, 1 for compiler errors or lint findings, and 2 for invalid command
arguments. Valid still performs parsing, type checking, ownership analysis, package validation, and
all other compiler checks; it disables only source-style findings.

`fcs_rule FCSCODE <off|warning|error>` in `foundation.package` configures one rule. A command-line
`--rule FCSCODE=<off|warning|error>` overrides it for that invocation. Source may suppress exactly
one following line with `// fcs:ignore FCSCODE reason`; an empty reason or dangling directive reports
`FCS9001`. Only the listed codes below are configurable. An explicit override enables a
Strict-only rule under Standard. Valid still disables every style rule, and `FCS9001` cannot be
configured or suppressed.

The compiler enforces these rules:

- `FCS1001`: Standard source stays within 100 columns and Strict source stays within 80 columns.
- `FCS1002`: multiline signatures place the opening parenthesis last and one parameter per line.
- `FCS2001`: every exported API symbol has attached `//` documentation under Strict.
- `FCS2002`: a Strict source file with more than 12 comment lines does not let comment lines outnumber source lines.
- `FCS3001`: postfix conditionals use simple condition and value expressions.
- `FCS4001`: a discarded `Result` has an adjacent reason comment under Strict.
- `FCS5001`: a public raw-pointer boundary documents its `SAFETY` contract under Strict.
- `FCS6001`: function control-flow remains within the active profile threshold.
- `FCS7001`: recognizes `NOTE` comment markers; off by default.
- `FCS7002`: recognizes `TODO` comment markers; off by default.
- `FCS7003`: recognizes `FIXME` comment markers; off by default.
- `FCS7004`: recognizes `SAFETY` comment markers; off by default.

Width uses four-column tab stops and counts each UTF-8 scalar start as one display column. Root test
sources are checked with the root production sources. Locked dependencies, SDK packages, framework
packages, and compiler-generated sources are excluded. Diagnostics use their original path, line,
column, and source span.

The language server runs the same engine after successful semantic analysis and publishes lint
findings with their configured warning or error severity. Changing `foundation.package` invalidates
workspace analysis, so an FCS profile or rule override is reflected without restarting the server.

The complete accepted policy and the compiler enforcement table are in
[foundation-code-standard.md](foundation-code-standard.md).
