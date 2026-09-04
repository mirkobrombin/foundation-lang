# Foundation Code Standard

Status: Foundation Code Standard 0.1 for the 0.1 compiler and SDK. Release and implementation
state is maintained in the [README](../README.md#status).

The enforcement table separates deterministic mechanical checks from review policy. Advisory rules
remain non-mechanical when a compiler cannot judge author intent, prose quality, API design, or
whether a helper improves the source.

## Profiles

Foundation tools expose three profiles. A project selects one profile in its manifest and CI uses
the same selection.

- `Valid` means that the compiler accepts the program. It contains no style requirements beyond
  syntax needed to parse the source.
- `Standard` is the default for applications and packages. The formatter applies stable layout
  rules and the linter reports its implemented Standard rules.
- `Strict` is intended for the SDK, public libraries, unsafe boundaries, and framework packages.
  It adds exported API documentation, discard reasons, raw-boundary documentation,
  comment-density review, and a narrower source width.

A stricter profile never changes program meaning. Each implemented diagnostic identifies its
profile and rule. Generated source is excluded when its generated header is recognized by the
compiler.

The package manifest selects a profile with a lowercase directive:

```text
format foundation.package/v1
name example.app
version 1.0.0
language 1
fcs standard
fcs_rule FCS1001 error
source src
```

Omitting `fcs` selects Standard. `fcs_rule` overrides one listed rule from the table below with
`off`, `warning`, or `error`.
Each rule appears at most once in a manifest. `foundationc lint <source-or-project>` uses the
manifest selection. `--profile valid`, `--profile standard`, or `--profile strict` overrides it for
that invocation. `--rule FCS1001=error` applies one invocation-only override; command-line
overrides win over manifest entries. An explicit override also enables a Strict-only rule under
Standard; Valid always disables style findings. Lint validates the complete compiler graph first and exits with
status 1 when either compiler errors or lint findings exist. An `error` override is published as an
error in editor diagnostics but does not change program meaning. Valid performs compiler validation
without style findings.

A motivated source suppression applies to the next source line only:

```foundation
// fcs:ignore FCS1001 ABI fixture preserves the external spelling.
const externalName = "this intentionally exceeds the selected source width for fixture parity"
```

The reason is mandatory. The compiler reports `FCS9001` for malformed suppressions, unknown rule
codes, attempts to suppress `FCS9001`, or a suppression with no following source line. Suppressions
do not apply to compiler errors. `FCS9001` cannot be suppressed, disabled, or severity-overridden.

## Compiler enforcement

| Rule | Standard | Strict | Status |
| --- | --- | --- | --- |
| `FCS1001` source width | 100 columns | 80 columns | Enforced |
| `FCS1002` signature layout | Required | Required | Enforced where layout is unambiguous |
| `FCS2001` exported API documentation | Optional | Required | Enforced |
| `FCS2002` comment density | Advisory | Required | Enforced |
| `FCS3001` simple postfix conditionals | Required | Required | Enforced |
| `FCS4001` discarded Result reason | Advisory | Required | Enforced |
| `FCS5001` public raw boundary SAFETY contract | Advisory | Required | Enforced |
| `FCS6001` function control-flow complexity | Required | Lower threshold | Enforced |
| `FCS7001` NOTE marker | Off | Off | Recognized, configurable |
| `FCS7002` TODO marker | Off | Off | Recognized, configurable |
| `FCS7003` FIXME marker | Off | Off | Recognized, configurable |
| `FCS7004` SAFETY marker | Off | Off | Recognized, configurable |
| Naming, prose quality, API design, and helper extraction | Source policy | Source policy | Advisory only |

Width counts Unicode scalar starts as one column and expands tabs to four-column stops. Lint checks
only root project sources. It excludes dependency, standard-library, framework, and
compiler-generated sources. Strict requires documentation for every exported type,
contract, attribute, function, method, field, and enum variant.
FCS2002 reports only after a source file has more than 12 comment lines and those lines outnumber
its nonblank source lines; smaller files remain a review concern rather than a mechanical finding.

## Stable source choices

The formatter preserves these equivalent author choices:

- an explicit `return value` or a final tail expression;
- a block conditional expression or a simple postfix conditional expression;
- inline or multiline parameter attributes;
- a short block on one line or the same block across lines when both satisfy the active profile;
- documentation paragraph wrapping when it already fits the configured width.

The formatter does not convert one accepted form into another merely to impose one visual style.

## Bindings and names

Use `const` unless a binding is reassigned or is the editable root of an `&` borrow. Use `var` only
for those two cases. `let` is not Foundation Language 1 syntax.

Package declarations, locals, parameters, and fields use `lowerCamelCase`. Types and exported
declarations use `UpperCamelCase`. An uppercase initial exports a package or type member; a
lowercase initial keeps it internal. Acronyms are treated as words except for standard type names
such as `UUID`.

The primitive machine types are lowercase: `bool`, `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`,
`u64`, `f32`, `f64`, `isize`, `usize`, `void`, and `never`. Nominal and managed types are
UpperCamelCase: `String`, `UUID`, `Option<T>`, `Result<T, E>`, and collection types. `int`, `float`,
and width-dependent aliases are not permitted.

Use `Target.From(value)` for every numeric type change. Do not imitate a cast with arithmetic or
route a checked Result through `discard`. When `From` returns `Result<T, NumberError>`, handle the
range, finiteness, or precision failure at the boundary where the conversion is requested.

## Results and exits

A tail expression is Standard for the ordinary successful result at the end of a short function.
An explicit `return` is Standard for an early exit and remains Valid for any result. Avoid deeply
nested tail expressions that hide the returned value.

Every `Result` must be returned, matched, unwrapped with `else`, passed to a consumer, or handled by
`discard`. Discarding a Result is Valid. Standard source makes the reason visible in the callee name
or an adjacent comment. Strict source gives every discarded error payload a reason.
`discard` remains the required spelling for any intentionally ignored owned value.

Use `else error { ... }` when the failure path needs the payload. Use `else { ... }` when the
failure itself matters but its payload does not. The shorter form is an explicit discard, not an
implicit error name, and the compiler still runs the payload destructor.

There is no `try`, `throw`, exception handler, or implicit error propagation. `panic` is reserved
for broken invariants and process-fatal conditions.

## Tests

Name tests after observable behavior. Keep setup inside the independently executed `test` body or
in an ordinary helper. Use `expect(condition)` for a checked condition, `fail(value)` for an
unexpected path, and `pass()` only when reaching the path is itself the assertion. Platform-specific
tests use `@target` rather than runtime platform branches.

## Conditionals

Use a block for every ordinary `if`, `else`, loop, and conditional branch. Braces are required even
for one statement. Indentation never changes meaning.

The only braceless statement form is a short divergent guard on one physical line:

```foundation
if !name return .Err(.EmptyName)
```

Its body must be `return`, `break`, or `continue`; it cannot have `else`. Standard limits the
condition and exit expression to one readable line. The active FCS1001 profile width applies to the
whole guard and does not prohibit the form.

Both conditional expression forms are Valid:

```foundation
const label = if ready {
    "ready"
} else {
    "waiting"
}

const code = 0 if ready else 1
```

The postfix form always requires `else`. Standard source uses it only when the condition and both
values are simple expressions without nested conditional expressions or side effects.

## Functions and closures

Named and anonymous functions use `fn`, a block body, and the same tail-expression rules. There is
no arrow lambda syntax. Anonymous parameter and result types may be omitted only when the expected
function type determines all of them.

```foundation
const scale fn(i32) i32 = fn(value) capture(factor) {
    value * factor
}
```

Function types preserve parameter modes: `fn(T) R` reads, `fn(&T) R` edits, and `fn($T) R`
transfers. Calls use the matching plain, `&`, or `$` spelling. Do not write the retired
`fn(view T) R` or `fn(edit T) R` forms in new source.

Use `transferable fn(P) R` only at a native executor boundary or in a type that can cross one.
The qualifier applies to the hidden function environment, not to argument ownership or permission
to call one value concurrently through aliases.
Use `<T transferable>` only when a generic function transports or captures its argument across
that boundary. Do not add the constraint to ordinary collection or transformation helpers.
Use `<T Contract>` when the generic body needs the statically dispatched method set of a nominal
contract. Do not replace it with a borrowed existential unless heterogeneous values are required.

Use named arguments when a value is otherwise unclear at the call site, or when later optional
parameters are skipped. Keep positional arguments before named arguments. Reordering named
arguments does not reorder evaluation, but Standard follows declaration order unless a different
order materially improves readability.

Name an enum payload when the name explains more than its type. Put literal match arms before the
binding arm for the same variant. Use the binding arm as the visible exhaustive fallback for
integer and String payloads; a boolean payload may enumerate both literal values. Keep guarded arms
before the unguarded arm for the same variant. Use `_:` only when the omitted variants genuinely
share one policy; name each variant when separate handling makes the domain clearer. An unguarded
wildcard is always last.

Keep a match arm as one expression when it states the complete decision. Use an arm value block
when the branch needs named intermediate values, mutation, or explicit cleanup. End a value branch
with its result expression; omit the tail for a `void` or always-exiting branch. Do not extract a
one-use helper solely to satisfy match syntax.

Standard uses a soft width of 100 characters. Keep a complete function, task, contract method, or
attribute declaration signature on one line when it fits. When it does not fit, end the opening
line after `(`, place exactly one parameter on each following line, and begin the closing line with
`)`. The result type, target clause, or opening brace remains on that closing line when it fits:

```foundation
fn NewUser(name String, initialScore i32) User {
    User { Name = name Score = initialScore }
}

fn RegisterUser(
    name String,
    initialScore i32,
    @audit actor User
) Result<User, RegistrationError> {
    register(name, initialScore, actor)
}
```

Do not keep the first parameter beside `(` after choosing multiline layout. Do not pack several
parameters onto one continuation line. Strict uses its 80-character FCS1001 width for source that
must remain readable in narrow panes or side-by-side review. Width affects layout diagnostics, not
language validity.

Every outer closure binding appears in `capture(...)`. A plain capture reads or copies, `&name`
captures an exclusive edit loan, and `$name` transfers ownership. Standard keeps short capture
lists inline and places longer lists one item per line.

## Ownership at call sites

A plain argument is read-only. `&value` grants one exclusive edit loan for the call. `$value`
transfers ownership and makes that place unavailable. Use the same marker on the parameter and at
the call site. Do not hide an ownership transfer in an implicit conversion or helper name.

`new Type { ... }` constructs a new owned value. `replace place with value` is Standard when the
old value is needed; direct assignment is Standard when it is not. Use `+=`, `-=`, `*=`, `/=`,
`%=`, `<<=`, or `>>=` only when the read-modify-write relationship is clearer than the expanded
assignment.
Compound assignment has the same edit-loan, overflow, division, ownership, and drop rules as its
ordinary operator. `String += String` is the only String compound form.
Its right-hand side must not read, edit, or transfer the root held by the compound assignment's
exclusive edit loan.

## Comments and documentation

Use `//` for line comments and `/* ... */` for nested block comments. There is no separate doc
comment punctuation. A contiguous `//` block immediately above a declaration, field, variant, or
parameter is Markdown documentation for that symbol. Elsewhere it is an implementation note.

Documentation describes contracts, ownership, failure conditions, units, and non-obvious policy.
It does not repeat names or types already present in the signature. `@param`, `@return`, and
`@throws` tags are non-Standard because the compiler already knows those facts.

Documentation is textual Markdown, not a second semantic language. Use backticks when prose names
a parameter, type, function, field, or literal. That convention improves reading but does not make
the text a symbol reference: rename, type checking, and diagnostics do not inspect names inside
documentation. IntelliSense obtains parameter names and types, ownership modes, attributes,
defaults, result types, and navigation targets from the compiler model.

The block above a function describes the callable as a whole. A block above an individual
parameter is optional and describes only behavior that the signature cannot express. It does not
repeat that parameter's name, type, or required status. Authors do not add a source-level
`Parameters` or `Returns` section merely to feed editor tooling.

`NOTE`, `TODO`, `FIXME`, and `SAFETY` are recognized labels. Their FCS7001 through FCS7004 marker
diagnostics are off by default and can be enabled per project or invocation. The compiler requires
`SAFETY` immediately before every lexical unsafe block regardless of lint configuration. Strict
source provides a safe wrapper around public unsafe boundaries. Documentation of exported APIs is
optional in Standard and required in Strict.

Official editors use the theme's ordinary comment presentation for both cases. Hover and generated
documentation render only comments attached to symbols by position, combined with signature facts
from the compiler model.

## Attributes and declarations

Parameter attributes may remain inline when the signature is short. Multiline layout is an author
choice. The formatter preserves a stable valid layout instead of forcing every attributed
parameter onto its own line.

`methods Type` may be split across package files. Standard groups closely related methods but does
not impose a file-per-method rule. A function without a receiver in that block is associated with
the type; no `static` modifier is used. Named construction uses `ctor`. Use `New` for the canonical
path and a precise UpperCamel name for alternatives.

Use a field default when the value is valid for every construction path and does not depend on a
partially initialized instance. Keep required state as a field without a default. Prefer a `ctor`
when construction requires validation, fallible coordination among fields, or a named policy
choice. Do not repeat a default in a literal unless the override is meaningful at that call site.

Services, actions, state machines, tasks, pipelines, sagas, and tests use their accepted
declaration syntax when those models apply. Routes use typed attributes on functions.
General-purpose functions and structs remain Standard when no application-architecture declaration
adds a useful invariant.

A pipeline step stays a small synchronous read-only transform returning `Result`. Use retry only when repeating
the function has no hidden external effect, and treat `max` as the total attempt count. A saga has
at least one compensation. Each saga step
uses the shared read-only input, returns `Result<void, E>` until the final output step, and declares
its compensation directly below it. Compensation must be idempotent because an external process
may still require replay policy even though one in-process saga execution calls it once. Match both
`.Step` and `.Compensation`; do not discard compensation details.

An action always spells its service receiver. Use `self` for observation, `&self` for mutation, and
`$self` for consumption. Do not hide service state as implicit locals inside an action. Service
construction uses `ctor`; service lifetime uses `@di.Scope(...)`. Constructor parameters provided
by another service or contract are dependencies, while every other type is an explicit
application-boundary input. Use `@di.Name(...)` and `@di.From(...)` only when a contract
intentionally has multiple providers. Actions use a stable
`@actions.Name(...)`; key bindings and policies remain repeatable attributes. The application plan
must pass before derived host code is accepted. Application and package methods belong to the
compiler model; do not check generated Foundation source into a project. Host synthesis must fail
when the selected lifecycle cannot be represented without weakening ownership or cleanup.

## Concurrency

Concurrent work uses `task`, `spawn`, and owned task handles. Standard keeps every spawned handle
in lexical scope and consumes it with `$handle.wait()` or transfers it to an explicit supervisor.
Dropping a live handle is a cancellation and join operation, never silent detachment. Strict source
does not use a supervisor without a documented lifetime, shutdown, and failure policy.

Cancellation uses `std.concurrent.CancellationSource` at the owning scope and moves a fresh
`Cancellation` token into each task. Cancellation is not reported as timeout, channel close,
operation error, or panic. Channels use directional endpoints. A `select` whose branches can
become ready together keeps source order unless the API names a fairness policy.

Calls into a foreign runtime do not let Foundation edit loans escape their call-scoped boundary.
The parked task frame pins a call argument until native completion, and no Foundation executor
observes that task concurrently. Managed pointers never cross the ABI. Standard marks a bodyless C
ABI import that may block with bare `@blocking`, calls it only as a standalone binding or `discard`
inside a task, and never hides that suspension inside an ordinary function. A callback adapter
uses `@callback` with an optional named cancellation symbol, completes its opaque operation exactly
once, and wraps raw status codes in a typed package result. Native panic or exception translation
belongs in the adapter and cannot unwind through Foundation code.

## Unsafe and native code

Raw pointer construction, arithmetic, dereference, slice storage access, and calls whose signature
contains a raw pointer occur inside `unsafe`. Each block is bounded and immediately preceded by a
`SAFETY` proof. Strict source provides a safe wrapper around the block. A safe C ABI signature made only
of specified ABI-safe values does not require an unsafe call site.

Platform selection uses `@target(...)`. Source does not contain preprocessor branches. Native
headers, libraries, and linker options belong in the package manifest rather than source comments
or shell scripts.

## Complexity

Keep functions whose control flow fits one screen and whose nested ownership and error paths remain
auditable. The linter reports `FCS6001` when a function exceeds 12 control-flow decisions or four
nested control-flow scopes in Standard, or seven decisions or four scopes in Strict. A diagnostic
never asks the formatter or compiler to extract a helper: the author decides whether to split the
policy.

Public APIs prefer domain types over repeated primitive tuples, explicit Result errors over panic,
and contracts over state inheritance. Structs remain final and do not form class hierarchies.
