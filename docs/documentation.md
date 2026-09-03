# API documentation

`foundationc documentation <source-or-project> -o <output.md> [--target <platform>]` validates the
source graph and writes one deterministic Markdown API reference. The optional target must match
the package lock. Documentation does not require an executable `main` function, so the command
works for reusable packages.

The reference contains only exported declarations owned by the root project. Standard-library,
Foundation framework, dependency, test-only, and compiler-derived declarations do not appear as
local API. A project containing several root packages receives one package section per package,
sorted by its declared name.

The compiler supplies declaration kinds, signatures, ownership modes, parameter types, attribute
targets, implemented contracts, parent contracts, fields, variants, and distributed methods.
Attached `//` blocks supply Markdown prose. A parameter appears in the Parameters section only
when that parameter has attached prose; its name and type remain visible in the signature without
duplicating them in comments.

Types, contracts, attributes, free functions, and distributed methods are sorted by name. Field
and enum variant order remains source order because it describes the declared data shape. Internal
declarations and members are omitted even when they have comments.

The command refuses a non-`.md` output path. It does not resolve names written inside prose,
rewrite Markdown links, or modify Foundation source files.
