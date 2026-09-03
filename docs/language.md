# Language specification

Status: Foundation Language 1 specification. The compiler and SDK remain on the 0.1 toolchain
line; implementation state is maintained in the [README](../README.md#status). Accepted and
rejected conformance fixtures exercise executable rules through the compiler entry point.

## Source text

Foundation source is UTF-8 and uses `.fn` as its only source extension. Compilers reject Foundation
source presented with another extension. Keywords and identifiers are case-sensitive. Identifiers
match `[A-Za-z_][A-Za-z0-9_]*`. String literals must decode
to valid UTF-8. A raw U+0000 byte is rejected in source, while `\0` represents an embedded zero byte
in the String value. `//` starts a line comment. `/* ... */` is a nested block comment. Statements
do not use terminators.

A newline stops an infix or postfix operator placed at the start of the next line from extending
the previous expression. A long expression continues when its operator appears before the newline.
Indentation has no semantic meaning.

## Executable grammar

```text
program          = package-declaration? import-declaration* attributed-declaration* EOF ;
package-declaration = "package" qualified-name ;
import-declaration = "import" qualified-name ("as" identifier)? ;
qualified-name   = identifier ("." identifier)* ;
attributed-declaration = declaration-attribute* declaration ;
declaration-attribute = target-attribute | attribute-application ;
target-attribute = "@" "target" "(" target-name ")" ;
target-name      = "linux" | "macos" | "windows" ;
attribute-application = "@" qualified-name "(" attribute-arguments? ")" ;
attribute-arguments = attribute-argument ("," attribute-argument)* ;
attribute-argument = (identifier "=")? expression ;
declaration      = struct | service | methods | enum | contract | attribute-declaration | function
                 | task | c-abi-function | state-machine | workflow | test-declaration ;
attribute-declaration = "attribute" identifier "(" parameters? ")"
                        "targets" "(" attribute-target ("," attribute-target)* ")"
                        "repeatable"? ;
attribute-target = "fn" | "struct" | "service" | "enum" | "contract" | "method"
                 | "ctor" | "action" | "field" | "variant" | "parameter" ;
struct           = "struct" identifier type-parameters? implementation-list?
                   "{" struct-member* "}" ;
implementation-list = "implements" implementation ("," implementation)* ;
implementation   = type ;
struct-member    = delegation | attribute-application* (struct-field | method | constructor) ;
service          = "service" identifier type-parameters? implementation-list?
                   "{" service-member* "}" ;
service-member   = delegation | attribute-application* (struct-field | method | constructor
                 | action) ;
delegation       = "delegate" identifier "as" type ;
struct-field     = identifier type ("=" expression)? ;
enum             = "enum" identifier type-parameters? "{" enum-variant+ "}" ;
enum-variant     = attribute-application* identifier
                   ("(" (identifier type | type) ")")? ;
contract         = "contract" identifier type-parameters? inheritance-list?
                   "{" contract-method* "}" ;
inheritance-list = "extends" type ("," type)* ;
method           = "fn" identifier "(" receiver ("," parameters)? ")" type function-block ;
constructor      = "ctor" identifier "(" parameters? ")" ("Result" "<" type ">")?
                   function-block ;
action           = "action" identifier "(" receiver ("," parameters)? ")" type function-block ;
contract-method  = attribute-application* "fn" identifier "(" receiver ("," parameters)? ")" type
                   function-block? ;
receiver         = "self" | "&" "self" | "$" "self" ;
function         = "fn" identifier function-type-parameters? "(" parameters? ")" type
                   function-block ;
c-abi-function   = "extern" "c" "fn" identifier "(" parameters? ")" type
                   "as" identifier function-block? ;
test-declaration = "test" string block ;
type-parameters  = "<" identifier ("," identifier)* ">" ;
function-type-parameters = "<" constrained-type-parameter
                           ("," constrained-type-parameter)* ">" ;
constrained-type-parameter = identifier "transferable"? type? ;
parameters       = parameter ("," parameter)* ;
parameter        = attribute-application* ("&" | "$")? identifier type ;
type             = ownership? type-value ;
type-value       = qualified-name type-arguments? | fixed-array | slice | function-type ;
function-type    = "transferable"? "fn" "(" function-type-list? ")" type ;
function-type-list = function-type-parameter ("," function-type-parameter)* ;
function-type-parameter = ("&" | "$")? type ;
fixed-array      = "[" integer "]" type ;
slice            = "[" type "]" ;
ownership        = "own" | "view" | "edit" ;
type-arguments   = "<" type ("," type)* ">" ;
block            = "{" statement* "}" ;
function-block   = "{" statement* tail-expression? "}" ;
statement        = binding | struct-destructure | assignment | return | discard | branch | loop
                 | result-else | expression-statement ;
binding          = ("const" | "var") identifier type? "=" expression
                 | "const" identifier type? "=" expression "else" identifier? block ;
struct-destructure = "const" qualified-name
                     "{" struct-pattern-field* "}" "=" expression ;
struct-pattern-field = identifier ("as" identifier)? ","? ;
assignment       = place ("=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=")
                   expression ;
place            = identifier (("." identifier) | ("[" expression "]"))* ;
return           = "return" expression? ;
discard          = "discard" expression ;
result-else      = expression "else" identifier? block ;
branch           = "if" expression block ("else" block)? ;
loop             = "while" expression block ;
expression-statement = expression ;
tail-expression  = expression ;
expression       = conditional ;
conditional      = logical-or ("if" logical-or "else" conditional)? ;
logical-or       = logical-and ("||" logical-and)* ;
logical-and      = bitwise-or ("&&" bitwise-or)* ;
bitwise-or       = bitwise-xor ("|" bitwise-xor)* ;
bitwise-xor      = bitwise-and ("^" bitwise-and)* ;
bitwise-and      = equality ("&" equality)* ;
equality         = comparison (("==" | "!=") comparison)* ;
comparison       = shift (("<" | "<=" | ">" | ">=") shift)* ;
shift            = term (("<<" | ">>") term)* ;
term             = factor (("+" | "-") factor)* ;
factor           = unary (("*" | "/" | "%") unary)* ;
unary            = ("-" | "!" | "~" | "&" | "$" | "new" | "own" | "view" | "edit") unary
                 | postfix ;
postfix          = primary (("." identifier type-arguments? ("(" arguments? ")")?)
                 | ("[" expression "]"))* ;
primary          = integer | boolean | string | identifier | call | struct-literal
                 | array-literal | contextual-enum-constructor | match-expression
                 | conditional-expression | function-expression | replace-expression
                 | "(" expression ")" ;
conditional-expression = "if" expression value-block "else" value-block ;
value-block      = "{" statement* tail-expression "}" ;
call             = identifier type-arguments? "(" arguments? ")" ;
function-expression = "fn" "(" closure-parameters? ")" type? capture-clause? function-block ;
closure-parameters = closure-parameter ("," closure-parameter)* ;
closure-parameter = attribute-application* parameter-mode? identifier type? ;
capture-clause   = "capture" "(" capture-item ("," capture-item)* ")" ;
capture-item     = ("&" | "$")? identifier ;
arguments        = argument ("," argument)* ;
argument         = identifier "=" expression | ("&" | "$")? expression ;
struct-literal   = type "{" field-initializer* "}" ;
field-initializer = identifier "=" expression ;
array-literal    = "[" arguments? "]" ;
contextual-enum-constructor = "." identifier ("(" expression? ")")? ;
match-expression = "match" expression "{" match-arm+ "}" ;
match-arm        = identifier ("(" match-payload-pattern ")")? ":"
                   (expression | match-arm-block) ;
match-arm-block  = "{" statement* tail-expression? "}" ;
match-payload-pattern = identifier | ("-"? integer) | boolean | string ;
replace-expression = "replace" primary "with" expression ;
boolean          = "true" | "false" ;
```

Struct fields and initializers do not use commas. A field declaration is `name Type`; an
initializer is `name = expression`.

## Foundation Language 1 source surface

The source forms below define the Foundation Language 1 grammar. They do not make an ownership-invalid or
type-invalid program valid, and implementation support requires passing conformance tests.

The 1.0 grammar replaces retired `let`, word ownership markers, and inline-only methods with
these forms:

```text
binding          = ("const" | "var") identifier type? "=" expression
                 | "const" identifier type? "=" expression "else" identifier? block ;
parameter        = attribute-application* parameter-mode? identifier type default-value? ;
parameter-mode   = "&" | "$" ;
default-value    = "=" expression ;
closure-parameter = attribute-application* parameter-mode? identifier type? ;
receiver         = "self" | "&" "self" | "$" "self" ;
argument         = argument-mode? expression | identifier "=" expression ;
argument-mode    = "&" | "$" ;
capture-clause   = "capture" "(" capture-item ("," capture-item)* ")" ;
capture-item     = argument-mode? identifier ;
methods          = "methods" type "{" (method | associated-function)* "}" ;
associated-function = function ;
field-default    = identifier type "=" expression ;
delegation       = "delegate" identifier "as" type ;
short-guard      = "if" expression (return | break | continue) ;
conditional      = "if" expression block "else" block
                 | expression "if" expression "else" expression ;
for-loop         = "for" loop-binding ("," loop-binding)? "in" expression block ;
loop-binding     = "&"? identifier ;
new-expression   = "new" struct-literal | "new" call ;
task             = "task" function-signature function-block ;
spawn-expression = "spawn" call ;
blocking-attribute = "@blocking" ;
callback-attribute = "@callback"
                   | "@callback" "(" "cancel" "=" identifier ")" ;
unsafe-block     = "unsafe" block ;
raw-pointer      = "*" type | "*" "const" type ;
test             = declaration-attribute* "test" string block ;
state-machine    = "state_machine" identifier type-parameters? "{"
                   state-declaration+ transition-declaration+ "}" ;
state-declaration = "state" identifier
                  | "state" identifier "(" identifier type ")" ;
transition-declaration = "on" identifier parameters?
                         "from" identifier ("," identifier)*
                         "to" identifier ("(" identifier ")")?
                         ("after" integer "." timeout-unit)? ;
timeout-unit     = "seconds" | "milliseconds" | "microseconds" | "nanoseconds" ;
workflow         = workflow-kind identifier type-parameters?
                   "(" identifier type ")" result-type
                   "{" workflow-step+ "}" ;
workflow-kind    = "pipeline" | "saga" ;
result-type      = "Result" "<" type "," type ">" ;
workflow-step    = "step" identifier "using" qualified-name retry-policy?
                   compensation? ;
retry-policy     = "retry" "exponential" "(" "max" "=" integer ")" ;
compensation     = "compensate" qualified-name ;
```

`service`, `action`, `state_machine`, `pipeline`, and `saga` are executable application
declarations. `route` remains a typed attribute application. `select` is a structured concurrency
expression.

Retired word ownership spellings are rejected and do not appear in the installed standard library.
The retired `let` spelling is also rejected; immutable bindings use `const`.

## Target selection

`@target(linux)`, `@target(macos)`, and `@target(windows)` select package-scope declarations for
one compilation target. A declaration has at most one `@target`. Unknown targets are errors.
Other `@Name(...)` forms resolve through the typed package attribute system. The parser checks
every branch for lexical and syntax errors, but removes inactive declarations and the syntax owned
by their bodies before package linking. They
therefore cannot introduce duplicate names, imports, types, or C symbols on another target.

The compiler uses its host platform by default. `check`, `emit-c`, `emit-c-header`, and `emit-metadata`
accept `--target linux`, `--target macos`, or `--target windows`. The target is deterministic
compiler input and must match a package lock. `build`, `run`, and `test` use the host C toolchain;
cross-compilation passes emitted C11 and the runtime to a matching target toolchain. Programs use
portable packages such as `std.platform` and do not use C preprocessor conditions.

## Entry point and functions

An executable declares exactly one `fn main() i32` or `fn main(args [String]) i32`. The
argument slice contains the command-line values after the executable name. Its Strings and storage
are borrowed for the call and released by the generated C entry adapter. Other functions may
appear in any source order. Parameters are immutable. Calls are statically resolved, checked for
arity and type, and evaluated left to right. The final expression of a non-void function is its
result. `return` exits
early; a returned expression begins on the same line as `return`. Every path through a non-void
function must produce its declared type. A `void` result may be used only as an expression
statement or a bare return. `main` cannot be called by source code, and `print` is reserved for the
builtin.

An `extern c` function cannot be `main` or generic. Its explicit name after `as` is the C symbol.
A declaration without a body imports that symbol; a declaration with a body exports its Foundation
implementation through that symbol. The C ABI form is valid only at package scope.

For a declaration with a body, Foundation visibility also controls the native library contract. An
uppercase Foundation name publishes the C symbol through Package Interface IR, generated public
headers, and shared-library export lists. A lowercase Foundation name creates a package-private
native entry: bundled C sources and objects may call the symbol, but it is omitted from PII, public
headers, and shared-library exports. Both forms receive the same checked C ABI validation. A
bodyless declaration remains a native import regardless of its Foundation visibility because the
package still requires that symbol from C.

The compiler-owned bare `@blocking` attribute marks a bodyless `extern c fn` import whose call may
block an operating-system thread. It does not accept parentheses or arguments and cannot annotate
an export, ordinary function, method, or function value. A blocking import call is a suspension
point and is currently valid only as a standalone binding or `discard` inside a `task`.

The compiler-owned `@callback` attribute marks a bodyless `extern c fn` import whose C symbol
starts an asynchronous native operation. The Foundation declaration returns `i32`; this is the
completion status consumed from the reactor rather than the C start function's return type. The C
start symbol returns `void` and receives the declaration's ABI-safe arguments followed by an
opaque `fdn_reactor_operation *`. `@callback(cancel = native_cancel)` also names a `void`
cancellation symbol that receives the active operation token. Bare `@callback` omits cancellation.
The two forms cannot be combined with `@blocking`, cannot be used as function values, and can be
called only as a standalone binding or `discard` inside a `task`.

`len(value) usize` returns the encoded byte length of a String, the fixed length of an array, or the
runtime length of a slice. It inspects its argument without consuming it. `len`, `print`, and
`panic` are reserved builtins and cannot be redeclared. `sizeOf<T>() usize` returns the storage size
of a value type for the current compilation target.

Return analysis treats every `while` as able to fall through. A non-void function must
therefore contain a return path after a loop unless an earlier statement already prevents
fallthrough.

## Function values and closures

`fn(i32, String) bool` is a function value type. Its parameter types appear inside the
parentheses and its result type follows them. Parameter modes use the same target spelling as named
functions: `fn(T) R` reads, `fn(&T) R` edits, and `fn($T) R` transfers. A read of a copy type is
passed by value; a read of a non-copy type is an internal loan. This choice is resolved after
generic substitution, so `fn(T) T` becomes a value call for `T = i32` and a borrowed call for
`T = String`. A named function can initialize a compatible value.
Generic named functions take explicit type arguments or infer them from the expected function
type. A closure inside a generic function inherits that function's type parameters. Function
values are called through a local binding with ordinary call syntax.

`transferable fn(P) R` is a distinct function type for values that may move between native
executors. A named Foundation function can initialize it because it has no captured environment.
A closure can initialize it only when every copied or owned capture is structurally transferable;
view and edit captures are rejected. A transferable function may be used where an ordinary
`fn(P) R` is expected, which discards the stronger guarantee. The reverse conversion is rejected.
When a generic function builds or transports such a closure, `T transferable` requires each
concrete type argument to pass the same structural transfer check. The constraint is available on
function type parameters; an unconstrained parameter is conservatively not transferable.

```foundation
fn identity<T>(value T) T {
    value
}

fn Consume($value String) void {
    print(value)
}

const operation fn(i32) i32 = identity
const result = operation(42)

const consume fn($String) void = Consume
const label = "owned"
consume($label)

const message = "worker"
const work transferable fn() void = fn() void capture($message) {
    print(message)
}

fn inspect<T>(value T) void {}

fn keep<T transferable>($value T) transferable fn() void {
    fn() void capture($value) {
        inspect(value)
    }
}
```

An anonymous function is a closure. Every outer binding it reads must appear after `capture`.
A bare name copies a copy type or takes a read loan. `$name` transfers a drop-requiring value into
the environment, and `&name` stores an exclusive edit loan.

An anonymous parameter type and the result type may be omitted when the surrounding expression
provides one complete function type. This includes an annotated binding, a function argument, a
struct field, an enum payload, or a typed conditional branch. Parameter modes remain explicit.
Without a complete expected signature, `FDN2184` rejects the anonymous function instead of
guessing from its body.

```foundation
const factor = 2
const scale fn(i32) i32 = fn(value) capture(factor) {
    value * factor
}

const explicit = fn(value i32) i32 {
    value + 1
}
```

Closures are reusable, so their bodies cannot consume a captured drop-requiring value. The language
has no single-use closure form. A closure with a read or edit capture is borrowed:
it cannot be returned, moved into an aggregate, or passed by value. The captured source remains
borrowed until the closure binding leaves the function in the current conservative analysis.

Every managed Foundation function value is move-only and drops deterministically. Named functions
use a null environment. A closure owns an allocated environment, a typed call pointer, and a drop
operation.
Dropping the value destroys owned captures and releases the environment. Function values may be
stored in structs and enum payloads; matching a payload moves it into the arm binding. Equality is
not defined for managed function values. Bind or match a managed function field or expression
before calling it; direct invocation is not supported.
Direct C function pointers are the separate copyable type documented by the C ABI section.

At package scope and on type members, an ASCII uppercase initial exports an identifier. A lowercase
initial or an initial `_` keeps it package-internal. This applies to functions, named types, struct
fields, methods, contract methods, and enum variants. Locals, parameters, and type parameters do
not carry visibility.

## Packages and project inputs

A project source declares one dotted package name before its imports and declarations. Imports are
file-local. `import example.math` binds the final segment `math`; `import example.math as numbers`
binds `numbers` instead. A package may appear once in a file. A qualifier accesses exported
declarations with `math.Add`, `math.Pair`, or `math.Identity<i32>`. Imports do not place
declarations in the unqualified scope.

A package path segment may equal a language keyword. When the final segment is a keyword, the
import requires an explicit non-keyword alias, as in `import foundation.pipeline as pipes`.
Keywords never become expression identifiers, so `pipeline.New()` remains a workflow declaration
ambiguity rather than a valid package-qualified call.

Files with the same package name share package-internal declarations. Crossing into another
package requires an imported qualifier and an ASCII uppercase initial on the type, function,
field, or variant. Import aliases must be unique in a file and cannot be shadowed by parameters or
local bindings. Every imported package must exist in the project, and the package graph must be
acyclic.

All compiler commands accept one source file or a directory. A directory with
`foundation.package` requires a matching target-specific `foundation.lock` and loads only the root
and locked dependency source trees. Registry dependencies come from the verified immutable cache;
path dependencies must still match their locked digest. A manifestless source directory
recursively discovers regular `.fn` files. Source paths are sorted bytewise and every project file
requires a package declaration. The complete executable graph declares exactly one entry point.
Diagnostics name stable root, `packages/`, or `std/` paths, and fatal traces retain package,
function, file, line, and column.

A package manifest selects one compatibility mode. `language 1` selects the permanent Language 1
source contract without tying the package to a compiler release. The pre-release
`sdk <requirement>` form remains accepted with its original toolchain-range behavior. A manifest
cannot contain both directives. `foundationc package init` writes `language 1`.

The optional manifest directive `fcs valid|standard|strict` selects compiler-backed source checks
for `foundationc lint` and the language server. It defaults to `standard`; the lint command can
override it for one invocation with `--profile`. Style findings are warnings and do not change
program meaning.

The optional `test_source tests` manifest directive names a test tree that cannot overlap the
production source tree. `dependency example.testing 1.0.0 registry default scope test` declares a
root test dependency. Production commands exclude both trees. `foundationc test` and language
service analysis include the root test tree and test-only dependency graph. A dependency package's
own test tree and test dependencies never enter the consuming graph. The lock records test root
edges with `scope test`; runtime is the implicit default. Package digests cover both declared source
trees, so changing a test source invalidates immutable cached content.

For single-file compatibility, a directly compiled single file may omit its package declaration.
Directory projects never receive that exception.

The compiler loads official standard-library `.fn` files from its configured
standard-library root and adds them to the project graph in normalized bytewise path order. Their
diagnostic paths begin with `std/`. User source imports an official package normally, such as
`import std.collections`; no compiler-only nominal implementation is attached to that import. SDK
installation manifests and `FOUNDATION_SDK_ROOT` define the configured root.

## Generics

Structs, enums, contracts, and functions may declare type parameters. A type application supplies
one argument for every parameter, and applications may be nested. Type parameters cannot shadow a
builtin or nominal type. `void` is still rejected in fields, payloads, and parameters after
substitution. A function parameter may add the structural executor-transfer constraint with
`T transferable`, require a nominal contract with `T Contract`, or combine both as
`T transferable Contract`. Every inferred or explicit concrete argument must satisfy every
declared constraint.

```foundation
fn bump<T Counter>(&counter T) void {
    counter.increment()
}
```

A nominally constrained generic uses static dispatch after specialization. The generic body may
call the complete effective method set of the constraint, including inherited requirements,
defaults, and an implementation reached through explicit delegation. This does not create an
existential value or a dynamic vtable fallback. Use `Counter` for a borrowed existential and
`own Counter` only when heterogeneous owned storage is required.

Function calls infer type arguments from their value arguments and may supply them explicitly, as
in `typeMarker<Outcome<i32, bool>>()`. Struct literals and payload enum constructors infer
arguments from initialized fields or the payload. An application must be written when some
parameter has no value from which it can be inferred, such as `Choice<i32>.None` or
`Outcome<i32, bool>.Err(false)`. Conflicting and incomplete inference are compile-time errors.

A generic named function can be specialized as a value, as in `identity<i32>`. The parser accepts
an explicit application before `(`, `.`, or `{`, and before a value terminator: `,`, `)`, `]`,
`}`, end of file, or a new statement line. The tokens inside the angle brackets must parse as a
bounded type list. A keyword, literal, assignment, or other non-type token ends the lookahead, so
an expression such as `x < y` cannot consume a later statement while searching for `>`.

`a < b > (c)` is intentionally read as a generic application because `(` is an active follower.
Write `(a < b) > (c)` when comparison is intended. The lexer keeps `>=` as one token. The
expression parser composes `<<`, `>>`, `<<=`, and `>>=` from adjacent delimiter tokens, so
nested generic closers remain ordinary `>` tokens.

Shift operands have the same integer type. The count must be non-negative for signed integers and
less than the width of the operand type. Left shift is checked arithmetic and panics when the
mathematical result is outside the operand type. Right shift is arithmetic for signed integers and
logical for unsigned integers. Addition binds more tightly than shift; shift binds more tightly
than comparison. The integer operators `&`, `^`, and `|` bind between equality and logical `&&`, in
that order from tighter to looser. Unary `~` complements every bit. Compound `<<=` and `>>=`
preserve the same rules and require an editable place.

Generic bodies are checked once with symbolic parameters. Operations that require a concrete
capability, including arithmetic and equality, are unavailable on an unconstrained parameter.
Recursive generic calls must preserve their type arguments; polymorphic recursion is
rejected.

Before C emission, FIR specializes each generic function and nominal value type reached by
`main`. Equal applications share one specialization. Unused generic declarations emit no C, and
specialization order is deterministic.

## Bindings and control flow

`const` declares an immutable binding. `var` declares a mutable binding. The type may be explicit
or inferred from the initializer. Names resolve through lexical block scopes and a declaration is
visible only after its initializer. Assignment to `const` is rejected. `let` is a reserved,
rejected retired spelling.

`const value = result else error { ... }` unwraps the `Ok` payload of a `Result`. The error binding
contains the `Err` payload. Use `const value = result else { ... }` when the failure payload is not
needed; the compiler still destroys that payload before leaving the branch. Both forms require the
failure block to exit with `return` or `panic`. `var` cannot use an `else` block. `discard
expression` explicitly consumes a value whose result is not needed.

`if` and `while` require a `bool` condition. `&&` and `||` short-circuit and preserve left-to-right
effects. A `while` condition is evaluated before every iteration. `for value in sequence` evaluates
its sequence once, holds the required loan for the loop, and drops iteration-local owners before
`break` or `continue` transfers control.

## Machine and managed types

Signed integers use `i8`, `i16`, `i32`, `i64`, or `isize`; unsigned integers use `u8`, `u16`,
`u32`, `u64`, or `usize`. Arithmetic is checked by the runtime. Overflow, underflow, division by
zero, and remainder by zero terminate the program with an arithmetic diagnostic; generated C does
not depend on signed-overflow behavior. Integer literals use `i32` unless an expected integer type
comes from their context. A negative literal cannot have an unsigned type.

`f32` and `f64` are IEEE 754 binary32 and binary64. Fractional and exponent literals default to
`f64`, and a contextual `f32` literal is range checked. Float arithmetic preserves IEEE infinities,
NaN, and signed zero. Numeric operands must have identical types unless an explicit `From`
conversion changes one side.

`bool` contains `true` and `false`. Ordering is defined for numeric operands of the same type.
Equality is available for every numeric type, `bool`, and `String`. Function values, structs,
enums, and symbolic generic parameters do not have equality.

`String` is immutable valid UTF-8 with an explicit byte length. A literal points at static storage.
`left + right` allocates a dynamic String, and `==` or `!=` compares exact bytes and lengths.
Dynamic storage moves with the String value and is released deterministically; assignment and
argument passing never perform an implicit clone. Integer indexing is not defined for String
because byte, Unicode scalar, and grapheme positions are different contracts.

`void` represents no value and is valid only as a function return type. `never` represents an
expression that does not complete and can satisfy any expected result type without producing a
value.

## Fixed arrays, slices, and indexing

`[N]T` is a fixed array of `N` inline elements. `N` is a non-negative compile-time integer in the
`i32` index range. A non-empty literal such as `[1, 2]` infers both element type and length. The
empty literal `[]` requires an expected fixed-array type. Every element must have the same type.
An array copies only when its element type copies; otherwise the whole array is move-only and its
live elements drop in reverse index order.

`value[index]` reads an array or slice element after a runtime bounds check. The index has type
`i32`; a negative index or an index at or above the length panics at the active Foundation source
location. Writing an indexed place requires a `var` root or an `&` slice parameter. A drop-requiring
element cannot move out independently, while replacement drops the old element before storing the
new one.

`[T]` in a plain read parameter contains a read-only element pointer and length. `&values [T]`
contains a mutable element pointer and length. Passing an array to either form erases the fixed
length without allocation. Slice loans are transient call arguments and cannot be stored or
returned.

## Value structs

A struct declares a nominal type with zero or more named fields and methods. Field names are unique
and every field type must be non-void. An empty struct is a unit-sized nominal value. Declarations
may refer to structs declared later in the file, but a struct cannot contain itself through a chain
of value fields.

A struct literal names every field exactly once. Initializers may appear in any order and are
evaluated in source order. Field access uses `value.field`. Structs may be bound, assigned as a
whole, passed, and returned. A struct containing only copy fields is copied. A struct containing an
owner, String, or other drop-requiring field is move-only. Field mutation requires a place rooted
in a `var` binding or an `&` parameter. An owned field cannot be moved independently; the
containing value must move as a whole or the field must receive a replacement through a mutable
place.

A complete struct pattern consumes a struct value or owner and creates immutable bindings for all
of its fields:

```foundation
const Packet { payload count as itemCount } = packet
```

The pattern names the struct declaration but omits generic arguments because the initializer fixes
them. Every field must appear exactly once. `as` changes the local binding name. The compiler
rejects unknown, duplicate, missing, and inaccessible fields. A value pattern moves its fields and
clears their source slots. An owner pattern also releases the outer allocation after the moves.
`var` patterns are invalid. A struct with a custom `drop` method cannot be destructured because
moving its fields first would violate the destructor contract.

A struct may declare one compiler-managed destructor with this exact signature:

```foundation
fn drop(&self) void {
    // Restore or release resources before automatic field cleanup.
}
```

Source cannot call `drop`. Deterministic cleanup calls it once before generated reverse-order field
cleanup. Declaring `drop` makes the struct move-only even when every field is a copy type. The
method cannot appear in a contract, cannot return an error, and panic still terminates without
unwinding. A generic struct
receives a specialized destructor for each reachable type application.

The generated representation of a struct with `drop` contains a private active bit. Construction
sets it, moving transfers it and clears the source, and cleanup clears it before calling the custom
method. Later lexical cleanup of a moved or already discarded slot is therefore inert. The bit is
not source-visible and no struct crosses the Foundation C ABI.

## Methods and contracts

A method is declared inside a struct or in a `methods Type` block in the package that owns the
type. Blocks may be split across package files and form the same method set as inline methods. The
receiver is written `self`, `&self`, or `$self` and cannot be renamed. A plain `self` method reads
the receiver. An `&self` method requires an editable value or edit loan. A `$self` method consumes
the receiver, and every later use of that binding is rejected. Methods inherit the struct type
parameters and cannot declare additional method-local type parameters in Language 1.

A contract declares method requirements and may extend more than one contract. Parent methods are
flattened in declared depth-first order, followed by new child methods. Equal signatures reached
through a diamond occupy one slot. A cycle or a same-name signature conflict is an error. A direct
child declaration overrides an equal inherited slot. A contract with no direct methods is valid
when its parents provide at least one effective method.

A contract method body is its default implementation. Calls on `self` use dynamic dispatch, which
means a concrete method overrides a requirement even when a default calls it. Two different
inherited defaults for one slot are ambiguous; a direct child declaration resolves that ambiguity.
A `$self` contract method cannot have a default.

`struct S implements C` is accepted only when `S` supplies every effective method with the same
receiver, parameter types, and result type, or the slot has a default. Generic contract arguments
are substituted before the comparison. Implementing the same contract more than once and naming a
non-contract after `implements` are errors. A cross-package implementation must expose every
required method.

`delegate field as C` inside `struct S implements C` delegates missing methods to that field. The
named contract must appear directly in the struct's `implements` list. The field must exist and
nominally implement `C`; delegation can continue through nested fields. A method declared on `S`
wins over the delegate. A `$self` requirement must be implemented locally on `S`, because an inline
field cannot be consumed independently from its enclosing value. Structs are final value types;
contracts compose behavior without class or state inheritance.

Concrete calls use static dispatch. A plain `value Contract` parameter reads a conforming value;
`&value Contract` receives an edit loan. A conforming struct converts without allocation. The generated
C value contains the borrowed data pointer and a typed, immutable vtable. Contract borrows remain
transient call values and cannot be stored or returned. An edit contract accepts only an edit
loan, while a read contract accepts either readable form. A borrowed contract cannot invoke a
`$self` method.

`own Contract` is a move-only existential value and may be stored, returned, nested in aggregates,
and borrowed as `view Contract` or `edit Contract`. A conversion from `own Concrete` consumes that
owner and allocates one existential wrapper. The wrapper retains the concrete data pointer, vtable,
and concrete drop operation. Dropping it destroys the concrete allocation exactly once. Borrowing
an owned contract does not allocate. Calling an `own` method requires a named owned contract binding
and consumes both wrapper and concrete value. Contract method arguments apply the same conversion
rules, including when the receiver is already dynamic.

## Algebraic enums and match

An enum declares a nominal tagged value with at least one variant. A variant is either a unit or
carries one payload. The payload may be named with `Variant(name Type)` or left anonymous with
`Variant(Type)`. Payload types must be non-void. Direct or indirect value cycles across
structs and enums are rejected because they have no finite representation.

Constructors use `Type.Variant` and carry an expression only when the variant has a payload. When
the payload is named, its constructor also accepts `Type.Variant(name = expression)`. When an
expected enum type is available, `.Variant` is sufficient. Dot selection is resolved against
the symbol model: a value selects a field and an enum type selects a constructor. `match` is an
expression. Its scrutinee fixes the enum type, so arms use unqualified variants followed by `:`.
Payload variants either bind one immutable local or compare an integer, boolean, or String literal.
Literal arms precede the binding arm for the same variant. Duplicate literals and arms after a
binding are rejected. An integer or String payload needs a binding arm to remain exhaustive; both
boolean literals cover a boolean payload. Every variant must be covered, all arm expressions have
one result type, the scrutinee is evaluated once, and only the selected arm is evaluated.

An arm may use a block when it needs statements before its result. A final expression is the arm
value. A block without one has type `void`; every reachable arm must still have the same type. The
short single-expression form remains canonical when no intermediate work is needed. A block that
always exits through `return`, `break`, `continue`, or a `never` expression is divergent and does
not constrain the result type of the other arms.

```foundation
match change {
    Add(value): {
        total = total + value
        total
    }
    Clear: 0
}
```

Enums containing only copy payloads use copy semantics. An enum with a drop-requiring payload is
move-only, and matching it moves that payload into the selected arm binding. Generic enums follow
the same rules after type substitution. Matching an enum through a read or edit borrow does not
consume it. Copy payloads enter the selected arm by value, while drop-requiring payload bindings
remain read-only borrows for the arm.

An arm may add `if condition` between its pattern and `:`. The condition must be `bool`. A payload
binding is read-only while its guard runs; the guard cannot consume it or move another outer
binding. A successful guard selects the arm and only then moves an owned payload into the arm
binding. A false guard continues with the next arm in source order. Guarded arms never establish
exhaustiveness because their conditions may be false.

`_:` is the wildcard pattern. It binds no value and covers every variant not selected by an
earlier arm. A guarded wildcard remains conditional and does not establish exhaustiveness. An
unguarded wildcard must be last; every later arm is unreachable. The wildcard cannot carry a
payload, while `Variant(name)` remains the form that binds one.

## Ownership representation

This section documents the pointer-backed ownership model. Function source uses plain read
parameters, `&` edit loans, `$` transfers, and `new` construction. The `own T`, `view T`, and
`edit T` forms remain type constructors; the old word-based parameter, receiver, capture, and
call-site modes are rejected.

`own T` is a non-null exclusive owner of one heap value. `own expression` allocates storage through
the runtime, moves the expression into it, and returns `own T`. Allocation failure panics at the
active Foundation source location. Source code cannot construct a null owner.

An owner moves when consumed by another binding, assignment, argument, return, struct field, or
enum payload. The old place becomes unavailable. Using a moved place, using a place moved on only
some continuing control paths, moving while borrowed, or leaving an outer owner moved after a loop
iteration is a compile-time error. Copy types keep copy semantics. A composite containing an owner
or String is move-only and receives generated move and drop glue.

`view T` is the compiler representation of a shared read-only borrow. `edit T` is the representation
of an exclusive mutable borrow. A plain parameter requests read access, `&name T` requests edit
access, and `$name T` requests transfer. Calls use the same plain, `&`, and `$` modes. Bindings,
stored fields, and indexed sequence elements are valid transient places. The borrowed place must
remain live for the call. Several reads may coexist, while an edit conflicts with every other
borrow. Creating an edit from a local owner requires a `var` binding. Borrows cannot be stored in
fields, payloads, or locals, and cannot be returned.

Field reads through an owner or borrow do not consume the base. A place rooted in a mutable local
or an `edit` parameter may update a field. Reading an owned field through a larger value does not
move it; the compiler rejects attempts to consume that field independently.

`replace place with value` evaluates `value` first, then evaluates `place` once, moves the old value
out, stores the replacement, and returns the old value. It works with mutable locals, fields behind
mutable values or owners, editable fields, and mutable array or slice elements. The place and
replacement types must be identical. A replacement cannot consume its destination, target a
borrow, store a borrowed closure, or run while the root place is borrowed. If replacement
evaluation panics, the place has not been evaluated or changed.

Live owners drop in reverse declaration order on normal block exit and return. Assignment drops an
old owned value before storing its replacement. `replace` transfers the old value to its result
without dropping it. `discard` consumes and drops an owned value
immediately. Composite drop glue destroys the active owned fields once. Moving a composite clears
the old owner slots, so later scope cleanup is harmless. `panic` does not unwind and runs no drop
glue.

Function values follow the same move and drop rules. A closure with copied or owned captures owns
its environment. A closure with view or edit captures also carries a lexical loan, which prevents
the captured binding or closure from escaping while the loan is live.

An owner edge breaks an inline layout cycle. Recursive declarations such as `Option<own Node>` are
finite and valid, while an inline `Option<Node>` cycle remains invalid. Generated C represents
owners and borrows as typed pointers; only owners release storage.

## Builtins

`print(value String) void` writes the value followed by one newline. It lowers to the stable
runtime operation `fdn_println`.

`Option<T>` and `Result<T, E>` are predeclared generic value types. Option provides `None` and
`Some(T)`. Result provides `Ok(T)` and `Err(E)`. Source cannot redeclare them or shadow them with a
type parameter. Their inline tag-and-payload representation does not allocate.

Every `Result` is must-use. It must be matched, returned, passed, unwrapped with `const ... else`, or
consumed with `discard`. The compiler rejects a dropped temporary, an unhandled binding, and an
assignment that replaces an unhandled result. Result handling is checked across branches, loops,
and exhaustive match arms. Option is not must-use.

`panic(value String)` is fatal and never participates in Result handling. It terminates the
current control path, so it may close a `const ... else` error block. Generated functions maintain a
thread-local source frame chain. Panic output lists every active Foundation frame with package,
function, closure, file, line, and column even when the C output is optimized. Runtime FFI adapters
can add an explicit `[native]` boundary frame.

## Target bindings, conditionals, and empty tests

Foundation Language 1 has two binding declarations. `const` creates an immutable binding and `var` creates
a reassignable or editable binding. Local storage duration, escape analysis, and constant folding
do not require a third binding keyword. The retired spelling `let` is rejected with a
targeted diagnostic.

An `if` with blocks may be a statement or an expression. Expression branches are exhaustive and
produce one common type. `value if condition else fallback` is the postfix expression form and
always requires `else`. A braceless `if` is valid only as one physical-line guard whose body is
`return`, `break`, or `continue`; it cannot have `else`. Every other control-flow body uses braces.

The compiler executes both value forms and all three one-line guards. `break` and `continue` are valid
only inside a loop. Before either jump, generated code drops every active owner introduced inside
that loop body, including owners in nested conditional scopes.

Only `bool` is accepted as an ordinary condition. The unary expression `!value` additionally has a
static empty-test meaning for `String` and collection types that implement the compiler-known
`Empty` capability. It is true when the value is empty. `if value` remains invalid for non-bool
values. Empty tests do not apply to Option, Result, UUID, pointers, or numbers, so absence and
emptiness cannot be confused. Language services expose the resolved `is empty` meaning through
hover and an optional inlay hint.

A nominal collection opts into the capability with a read-only `fn IsEmpty(self) bool` method.
The method cannot edit or consume the receiver. Arrays, slices, and String use their stored length
without a method call.

## Target ownership spelling

Parameter and call-site modes use the same marker. A plain parameter reads its argument for the
call and cannot retain or mutate a non-copy value. `&name Type` receives an exclusive edit loan and
must be called with `&place`. `$name Type` consumes a value and must be called with `$place` when
the argument is a non-copy place. After the transfer, that place is unavailable. A temporary is
already fresh, and a copy value does not change ownership, so neither requires a redundant `$`.

The same rules apply to receivers (`self`, `&self`, `$self`) and closure captures
(`capture(value, &editable, $owned)`). A loan lasts through the call or, for a closure, through the
closure value's lexical lifetime. A compiler may shorten the lifetime when control-flow proof is
complete but cannot make a conflicting edit or use-after-transfer valid.
A borrowed temporary owner remains live through the outer call expression and is destroyed
immediately after that expression completes.

`new Type { ... }` creates a fresh owned value. Ownership is a property of the value and place, not
part of its nominal type spelling. The compiler may choose stack, heap, or inline storage when that
choice is not observable. `$` transfers the value rather than promising one allocation strategy.
`replace place with value` keeps the existing evaluation and drop contract. Compound assignment
uses `+=`, `-=`, `*=`, `/=`, `%=`, `<<=`, and `>>=`. It takes the same edit loan as ordinary
assignment, evaluates its place once, and applies the checked arithmetic, division, or shift rule
of the corresponding binary operator. `String += String` concatenates; other String compound
operators are rejected.
The edit loan begins after the place is materialized and remains exclusive through evaluation of
the right-hand side. Reading, editing, or transferring the same root from that expression is a
conflicting access.

The compiler rejects the retired `view`, `edit`, and `own` parameter, receiver, call, and capture
spellings with targeted diagnostics. It also rejects the retired capture clause without
parentheses.

## Target types and construction

The complete machine scalar set is `bool`, signed `i8` through `i64`, unsigned `u8` through `u64`,
`f32`, `f64`, `isize`, `usize`, `void`, and `never`. Width-dependent `int` and `float` aliases do
not exist. `String`, `UUID`, Option, Result, and user types are nominal UpperCamelCase types.

`UUID` is an executable standard prelude value type with a 128-bit non-allocating representation.
`UUID.Nil()` produces all zero bits; `IsNil`, `Equal`, and `Compare` inspect values without
allocation. `UUID.Parse` accepts canonical RFC 9562 text in either hex case, and `String` emits the
lowercase canonical form. `UUID.NewV4()` uses platform entropy. `UUID.NewV7()` is time-ordered and
strictly monotonic within one process, including when the wall clock repeats or moves backward.
The type, parser, formatter, and comparisons live in the Foundation standard library. The runtime
supplies only platform entropy, wall-clock milliseconds, and process synchronization. The parser
and type checker have no UUID-specific rule.

Exported nominal types declared by `std.prelude` resolve without an import. This rule is general:
it gives standard value types their short spelling without turning each one into a compiler
builtin. Package-local declarations take precedence over the implicit prelude.

A struct field may declare a default initializer with `field Type = expression`. A literal may
omit that field. Explicit field expressions run first in literal source order, then each omitted
initializer runs in field declaration order. An explicitly initialized field never evaluates its
default. Every construction evaluates its own defaults; the compiler does not cache their values.
A missing field without a default is an error.

A default is checked as a zero-argument function in the declaring package with the struct's type
parameters. It may call package-private functions and allocate or fail like ordinary Foundation
code. It has no `self` and cannot read another field under construction. This keeps initialization
order visible and prevents partially initialized values from escaping. A panic trace identifies
the frame as `Type.field default` at the field declaration.

Struct literals remain available directly. Construction that needs a named path uses
`ctor Name(...)`. The owner is the implicit success type, and a fallible constructor spells only
`Result<E>`. Constructors have no receiver and must initialize or return their owner. Multiple
constructors may coexist; call sites select one as `Type.Name(...)`. Editor hover presents a
field's declared type and default expression while keeping compiler-generated initializer
functions out of symbols and completion.

A function declared in `methods Type` without a receiver is associated with that type and is called
as `Type.Function(...)`. No `static`, `public`, or `private` modifier exists. Case controls package
visibility and receiver presence controls whether a function is associated or an instance method.

Numeric conversions use `Target.From(value)`. A conversion that can represent every source value
returns `Target`. A conversion that can overflow, underflow, produce an invalid value, or lose
information returns `Result<Target, NumberError>`. String parsing uses named Parse operations rather
than `From`, and there are no unchecked implicit numeric conversions.

Decimal integer literals are contextually typed and default to `i32`. Fractional and exponent
literals are contextually typed and default to `f64`. A literal outside its contextual type is a
compile-time error. Arithmetic requires identical operand types. Integer addition, subtraction,
multiplication, division, remainder, and signed negation trap on overflow; division by zero traps.
Floating-point arithmetic follows IEEE 754 binary32 or binary64 behavior, including infinities and
NaN, and `%` is available only for integers.

`NumberError` has the contextual variants `OutOfRange`, `NonFinite`, and `PrecisionLoss`. A checked
conversion succeeds only when the target can reproduce the source value exactly. This makes
narrowing, signedness changes, integer-to-float conversions beyond the exact mantissa range, and
`f64` to `f32` explicit Result-producing operations. Same-type conversions, safe integer widening,
and `f32` to `f64` return the target value directly.

`never` is the bottom type used by expressions that cannot complete. `panic` returns `never`, and a
function may declare a `never` result when every path diverges. A `never` expression can satisfy a
branch that otherwise produces a value, but no `never` value can be constructed, stored directly,
or returned by a task.

## Target methods, contracts, enums, and iteration

A package may add intrinsic methods to its own type inside the struct or in any number of
`methods Type` blocks. Blocks across files form one method set. Duplicate signatures are errors and
another package cannot add an intrinsic method. Language services expose every contributing
declaration with source mappings. Official editor clients present those locations in a native peek
view, where edits apply directly to the selected original file.

Contract inheritance and default methods keep their existing semantics. Delegation is written
`delegate field as Contract` inside the implementing struct. It supplies missing contract methods
from that field. A local method wins. Two delegated candidates for the same unresolved method are
an error, and consuming requirements must still be implemented by the outer struct.

Enum payload fields may be named. Match arms use contextual variant names and may bind payload
fields. Literal patterns such as `Stop(0)` compare with the literal; an identifier is a new immutable
binding. Match remains exhaustive. Multiple alternatives may share a body only when they bind the
same names with the same types; the exact alternative spelling remains reserved until a reviewed
snippet is accepted.

`[N]T` is a fixed array and `[T]` in a read parameter is a sequence view. `&values [T]` is an
editable sequence view. Owning dynamic collections use nominal standard-library types such as
`List<T>`. `for value in sequence` reads elements, `for index, value in sequence` also binds a
zero-based `usize` index, and `for &value in sequence` requires an editable sequence and loans each
element for one iteration.

An iterator provides `fn Next(&self) Option<T>`. `for` owns a temporary iterator or holds an edit
loan on an iterator place until loop exit. Iterator values are produced values, so editable element
bindings remain specific to editable sequences. `range(start, stop, step = value)` is a standard
prelude iterator rather than grammar. Its stop is exclusive and a zero step panics as a violated
programmer precondition.

Named arguments are checked against function, associated-function, concrete-method, and contract
method parameter names. Positional arguments cannot follow a named argument, and each parameter may
be supplied once. Argument expressions retain source evaluation order even when their values are
passed to parameters in another order.

The compiler executes these forms for fixed arrays, `[T]` sequence views, and structural iterators,
including loops that suspend inside a task. Non-copying sequence elements become implicit read
bindings, editable elements remain exclusive loans, and the sequence or iterator loan lasts until
loop exit. Indices and `len(...)` use `usize`.

## Target comments, imports, and native boundaries

`//` is the only line-comment spelling. A contiguous block immediately above a declaration, field,
variant, or parameter is Markdown documentation for that symbol; the same syntax elsewhere is an
implementation note. `/* ... */` is a nested block comment and does not attach documentation.
Attached documentation is retained for hover, completion, generated reference material, checked
code examples, and Markdown rendering. Documentation text is not resolved as code: backticked names
remain prose, rename does not edit them, and unknown names inside comments do not produce compiler
diagnostics. Editor signatures, type links, and navigation come from the semantic model instead.
`NOTE`, `TODO`, `FIXME`, and `SAFETY` are recognized by tooling but do not change runtime semantics.

Typed attributes on parameters may appear inline or on separate lines. Ownership markers stay next
to the parameter name, as in `@inject &users UserStore`, because the attribute describes the
binding source while `&` describes access.

Case-based visibility and import aliases keep their existing meaning. Test-only dependencies use
`scope test` in the manifest and require a separate `test_source` tree. Package resolution omits
them from normal builds and includes them in `foundationc test` and editor analysis.

An `extern c` declaration containing only the specified safe ABI types may be called from safe code.
A raw pointer or an ABI type without a safe representation makes the declaration unsafe and every
call must occur in `unsafe`. Native headers, libraries, object inputs, and link options are declared
per target in the package manifest. `@target` selects Foundation declarations; C preprocessor
conditions are not Foundation source control flow.

Raw pointer types are `*T` and read-only `*const T`. `*void` and `*const void` represent opaque C
handles and cannot be dereferenced or used for arithmetic. A mutable pointer converts to the
corresponding read-only pointer, and any typed pointer converts to a `void` pointer with compatible
mutability. Reverse casts are not implicit. The current portable ABI surface permits machine scalar,
`void`, nested raw-pointer pointees, and checked C-layout structs.

Pointer construction with `null<P>()`, where `P` is the complete raw-pointer type, pointer arithmetic
by a `usize` element offset, dereference, slice `.pointer` access, `pointerCast<P>(pointer)`, and
unsafe native calls are valid only in a lexical `unsafe { ... }` block. `pointerCast` accepts a raw
pointer and produces another raw pointer type or a direct C function pointer. `isNull(pointer)`
safely inspects either form and does not dereference. `cString("literal")` produces a
process-lifetime, null-terminated `*const u8`; its argument must be a literal. Every unsafe block
has an immediately preceding `// SAFETY: ...` proof. Safe references and slices cannot be forged
from a pointer without proving alignment, lifetime, bounds, and exclusivity. Those guarantees resume
at the boundary when a safe value leaves the block. `unsafe` does not disable type checking,
ownership, Result must-use, or target checks.

## Tasks, channels, and selection

A `task` is a suspendable typed function lowered to a stackless state machine. It has no implicit
thread affinity. `spawn call` enqueues it on the active Foundation executor and returns an owned
`Task<T>` handle. The scheduler may use one executor thread or a bounded pool without changing
source semantics. There is no `async` or `await` modifier and no implicit detached work.

`$pending.wait()` transfers and consumes the task handle, drives or suspends the current executor
until completion, and moves the result exactly once. A second wait is therefore a compile-time use
after move. Multiple observers require an explicit `SharedTask<T>` abstraction. Dropping a live
handle requests cancellation and joins it; detachment requires an explicit supervisor supplied by
a framework package.

Cancellation is an explicit `std.concurrent.Cancellation` value passed through APIs that can
observe it. A `CancellationSource` remains with the owner, while each `Token()` creates a separate
owned reference that can move into a task. `Cancel()` requests cancellation for every token.
`IsRequested()` also observes cancellation requested by structured task drop. Cancellation is
distinct from a channel close, timeout, recoverable operation error, and panic. A returned owned
value transfers from the task into the waiting caller under the ordinary `$` rules.

Foreign runtimes never become Foundation executors. C uses the stable C ABI directly. C++ uses an
`extern "C"` shim, Zig exports its C ABI, Rust exports `staticlib` or `cdylib` C symbols, and Go
exports `c-archive` or `c-shared` symbols. A package can distribute those artifacts with generated
Foundation bindings, but importing a Go module or Rust crate does not merge its type system,
collector, ownership model, or scheduler with Foundation.

Managed pointers cannot cross a runtime boundary. Calls exchange copied ABI values or opaque
handles with an explicit destroy operation. Potentially blocking foreign calls run through the
blocking executor; callback-based libraries complete a Foundation task through a generated bridge.
Foreign exceptions and panics cannot unwind through Foundation frames and must be converted to a
typed status or terminate at the native boundary.

The compiler lowers task waits, blocking C calls, and callback C operations used as standalone bindings
or void statements to numbered stackless states. The task leaves the executor queue while its
child or native operation runs, then resumes at the suspension point without replaying earlier
effects. Locals, owned parameters, results, cancellation, branch position, and loop position
survive in the task frame. Other expression placements report FDN2168 until the task normalization
pass can spill their intermediate values.

An `@blocking` call copies ABI-safe arguments into its task frame, submits one generated callback
to the bounded native worker pool, and moves or drops its result exactly once after completion.
The completion queue wakes the owning executor while channel deadlines remain active. Structured
cancellation cannot stop arbitrary foreign code: the frame stays alive until the native call
returns, then the task resumes as cancelled.

The first standard package built on this rule is `std.fs`. `ReadText` and `ReadTextLimited` are
tasks whose package-private `@blocking` import reads and validates UTF-8 on the bounded worker
pool. The default call caps the result at 16 MiB; the limited form returns `Error.TooLarge` when
the caller's byte cap would be exceeded. Ordinary file reads do not use the callback reactor.

An `@callback` call also pins its ABI-safe arguments in the task frame. Its generated start adapter
registers one reactor operation and passes the opaque token to native code. An edit argument may
publish output into the pinned frame before `fdn_reactor_complete(operation, status)`. Completion
must occur exactly once, including after the generated cancellation adapter requests cancellation.
The task frame remains alive until completion is drained by its owning executor.

`std.net` combines these paths for TCP clients. `Connect` resolves host names on the bounded
blocking executor and attempts sockets through the callback reactor. A successful connection
splits into independently owned read and write halves. Text reads validate UTF-8 and enforce an
explicit byte cap. Exact and incremental byte reads return owned `std.bytes.Bytes` without text
validation, and complete byte writes keep their transferred storage alive across suspension. A
failed or cancelled complete write closes its half because a prefix may have already reached the
peer.
Filesystem watchers use the same callback protocol and complete one bounded polling watch per
task. Parallel Foundation workers use transferable task frames and the fixed worker-pool boundary.

`foundation.worker.Supervisor` is the cooperative detachment boundary. `Start` consumes a
`Task<void>` and keeps it alive beyond the lexical scope that spawned it. `Shutdown` waits for all
supervised tasks. `Cancel` requests structured cancellation before joining them. Dropping a live
supervisor applies `Cancel`, so no supervised frame outlives its owner. The `Task<void>` restriction
requires a task that can fail to handle or route its `Result` before it is detached.

`foundation.worker.Pool` is the explicit parallel boundary. `NewPool(workers)` creates a fixed
number of native worker threads, and `Start(spawn work(...))` transfers one `Task<void>` frame to
their bounded executor set. The argument must remain a direct `spawn` expression so the compiler
can verify every captured call argument. Scalars, `String`, arrays, and structural aggregates
can transfer recursively. A function value can cross only when its type is `transferable fn`; the
qualifier proves that its hidden closure environment satisfies the same structural check.
`Channel<T>`, `Sender<T>`, and `Receiver<T>` can transfer when `T` does. A custom-drop struct
remains executor-local unless it explicitly carries
`@concurrent.Transferable()`, and the compiler still checks every field recursively. Borrows,
slices, contract values, task handles, and unmarked custom-drop structs report FDN2185.

`@concurrent.Transferable()` is an ownership transfer assertion, not permission for concurrent
aliases. The value still moves exactly once. Standard containers use it only where their cleanup
and allocator contract are valid on every Foundation executor. Native packages must not mark a
thread-affine handle; its package ABI remains responsible for the claim.

Each pool worker runs the transferred state machine, its structured children, and any channels it
creates on one worker-local cooperative executor. `Shutdown` drains and joins; `Cancel` requests
structured cancellation for queued and active frames before joining. A running CPU poll observes
cancellation at its next suspension or poll boundary. The pool does not turn arbitrary foreign
calls into safe parallel work; native thread safety remains part of the package ABI contract.
Each `print` call writes one complete line without byte interleaving between workers. The order of
lines produced by parallel tasks is unspecified.

`Sender<T>` and `Receiver<T>` are directional channel endpoints. Inside a task,
`sender.send(value)` returns `Result<void, ChannelError>` and `receiver.receive()` returns
`Result<T, ChannelError>`. Both operations suspend the task instead of blocking the executor.
`ChannelError.Closed` reports that the opposite endpoint can no longer complete the operation,
while `ChannelError.Cancelled` reports structured task cancellation. `Timeout` is reserved for the
`select` timeout surface. A failed send consumes and drops its value, so ownership never becomes
ambiguous. A `select` evaluates endpoint expressions once, waits until a branch is ready, and
chooses the first
ready branch in source order. A timeout branch is eligible only when no operation completed before
its duration. Literal durations use `seconds`, `milliseconds`, `microseconds`, or `nanoseconds`.
A dynamic timeout expression has type `u64` and carries nanoseconds. Its value is evaluated once,
and deadline addition saturates instead of wrapping. Source-order priority is deterministic; fair
scheduling is an explicit higher-level policy.

The transport is synchronized across pool workers. A remote channel operation posts its wake to
the mailbox owned by the parked task's executor; foreign threads never mutate that executor's
ready queue. `select` subscribes to every participating channel while their locks are held in a
stable order, so registration cannot lose an intervening send, close, cancellation, or timeout.

Operation branches bind only successful receive payloads. `Closed` and `Cancelled` use one
mandatory `else error` branch, while timeout remains a separate branch. This keeps payload locals
initialized and makes every non-success path visible:

```foundation
task consume(messages Receiver<String>, stopped Receiver<void>) Result<void, ChannelError> {
    while true {
        select {
            const message = messages.receive(): print(message)
            stopped.receive(): return .Ok
            timeout 5.seconds: return .Err(.Timeout)
            else error: return .Err(error)
        }
    }
}
```

Timeout literals accept `seconds`, `milliseconds`, `microseconds`, or `nanoseconds` and use a
monotonic deadline computed once when control enters the statement. Send operands are also
evaluated once. Every send payload is prepared before suspension; the selected payload transfers
once and unselected or failed owned payloads are dropped before the branch body runs.

`channel<T>(capacity)` opens a move-only `Channel<T>` pair. A complete pattern transfers both
directions into independent lexical owners:

```foundation
const Channel { sender receiver } = channel<String>(0)

task deliver(sender Sender<String>) Result<void, ChannelError> {
    const delivered = sender.send("ready")
    delivered
}

task next(receiver Receiver<String>) Result<String, ChannelError> {
    const received = receiver.receive()
    received
}
```

`sender.clone()` creates another owned sender handle for the same channel. Cloning is available
only on `Sender<T>` because multiple producers preserve one FIFO transport, while multiple
receivers would introduce implicit competing consumption. Each clone must be transferred or
dropped independently. The receive side observes `ChannelError.Closed` only after the final sender
handle disappears.

Dropping the pair closes both directions. Dropping a `Sender<T>` or `Receiver<T>` closes that
endpoint reference, wakes operations that can no longer complete, and releases the shared channel
state after the last endpoint disappears. Channel payloads cannot contain borrowed values.

The compiler and runtime implement endpoint construction and ownership, bounded FIFO
buffers, zero-capacity rendezvous, sender cloning, endpoint reference counts, send and receive suspension, close
wakeups, structured cancellation wakeups, payload drop callbacks, deterministic selection, and
monotonic timeout wakeups.

## Application declarations

Application declarations are compiler-parsed typed source forms whose runtime behavior comes from
Foundation framework packages. The compiler validates their static graph and lowers versioned
metadata and ordinary functions; it does not hard-code HTTP servers, containers, queues, or retry
engines.

A `state_machine` declares a closed state set and typed events. A transition names its source and
destination states and maps event payload values into state payload fields. Generated transition
methods return a typed transition error for an invalid source state. Guards and effects are typed
functions; framework execution never discovers them through runtime reflection.

```foundation
state_machine Order {
    state Draft
    state Submitted
    state Paid(receipt i32)
    state Cancelled(reason String)

    on Submit from Draft to Submitted
    on Pay(receipt i32) from Submitted to Paid(receipt)
    on Cancel($reason String) from Draft, Submitted to Cancelled(reason)
}

fn submit(&order Order) bool {
    order.Submit() else transition {
        discard transition
        return false
    }
    true
}
```

The machine value is an exhaustive enum, so ordinary contextual construction and `match` inspect
its state. Each `on Event` declaration generates an edit method with the event parameters and the
return type `Result<void, MachineTransitionError>`. The generated error enum contains
`InvalidState`. A failed transition leaves the machine unchanged. A successful transition drops
the previous state payload before installing the destination payload. A non-copy payload uses a
`$parameter` event input because the destination state owns it; copy payloads use plain inputs.
An expression-level `else error { ... }` handles `Result<void, E>` without inventing a void local.
The shorter `else { ... }` form explicitly ignores and destroys the error payload. Both error
blocks must exit. A generic state machine specializes its enum, transition methods, and timeout
accessors for every reachable type application. State payloads and event parameters may refer to
the machine type parameters under the same ownership and transfer rules as ordinary generic enums
and functions.

`foundationc emit-fsm` renders the checked transition graph as deterministic Mermaid or Graphviz
text. It consumes FIR rather than parsing tags or reflecting over a runtime value. When a project
declares more than one machine, `--machine Name` is required. Foundation values select their
starting variant explicitly at construction, so the renderer does not invent an initial arrow;
closed source-state lists replace wildcard transitions.

A `service` is a final value type that groups state and methods under a framework lifecycle. It can
implement contracts and use distributed `methods Service` blocks like a struct. An `action` is a
typed service method retained as static application metadata. Its receiver is always explicit:
`self` observes the service, `&self` edits it, and `$self` consumes it. An action outside a service
or without a receiver is invalid.

Construction is language syntax. Dependency classification and lifetime are Foundation framework
policy:

```foundation
import foundation.actions
import foundation.di

@di.Scope(.Singleton)
service Accounts {
    store UserStore

    ctor New(@di.From("primary") $store UserStore) {
        Accounts { store = store }
    }

    @actions.Name("users.register")
    @actions.Key("ctrl+n")
    @actions.Policy("users.write")
    action Register(self, command RegisterUser) Result<User, RegisterError> {
        self.store.Register(command)
    }
}
```

The compiler checks the owner, attribute targets, argument types, initialization, and complete
constructor body. A constructor has no receiver. Its owner type is the implicit success type, so
an infallible constructor omits a return type and a fallible constructor writes `Result<E>`, which
means `Result<Owner, E>`. A parameter whose type is provided by a service or implemented contract
is a dependency. Every other parameter is an application-boundary input. `@di.Name("name")` names
a provider and `@di.From("name")` selects it when a contract has more than one implementation.

`foundationc emit-metadata` emits declarations with `kind: "service"`, `kind: "ctor"`, and
`kind: "action"`.
`foundationc emit-app-plan` selects the sole constructor, or `ctor New` when a service declares
multiple constructors, orders dependencies before consumers, and emits
`foundation.application/v1`. Planning rejects generic or receiver constructors, invalid return
types, missing and ambiguous providers, dependency cycles, edit injection, captive owned
dependencies, duplicate action names, and duplicate key bindings. Constructor failure remains a
typed `Result<Service, E>` in the plan. A `$self` action consumes its owner and therefore requires
a transient service; scoped and singleton actions must preserve their service instance. A
generated Foundation host consumes the plan without runtime reflection.

The compiler derives package methods and application host declarations before checking ordinary
function bodies. Binding, validation, DI, actions, and web APIs therefore work on a clean checkout
without generated project sources. The language server reads the same semantic model and exposes
the derived signatures for hover, completion, navigation, and signature help.

`foundationc emit-app-host <project> -o <source.fn>` remains an inspection command for the
equivalent Foundation source; projects do not compile or check in that artifact. The derived host
contains `FoundationApplication`, `BuildFoundationApplication`, and one typed method per action. It
stores singleton services, preserves boundary input ownership, constructs providers before
consumers, and refuses to overwrite source without its generator marker. Transient action owners
and their transient dependencies are reconstructed for every invocation; activation inputs become
typed parameters on the generated action method, while singleton dependencies remain shared. If a
startup constructor is fallible, every fallible startup constructor must use the same application
error type `E`; the generated builder returns `Result<FoundationApplication, E>` and propagates
failures explicitly. `FoundationApplication.NewScope(...)` constructs one instance of each scoped
service, returns `Result<FoundationScope, E>` when scope creation can fail, and keeps scope inputs
at that boundary. Scoped actions receive the scope explicitly; transient action graphs may resolve
scoped services from it. Dropping a scope destroys its stored services in reverse dependency order
without touching application singletons. Fallible transient constructors in one action activation
must share one error type `E`. The generated action method returns `Result<R, E>`, maps a `void`
success to `.Ok`, and preserves an action that already returns `Result<R, E>` without adding a
second Result layer. A different action error type remains nested so activation and execution
failure cannot be confused.

The derived host also contains one payload struct per parameterized action, separate
`FoundationAction` and `FoundationScopedAction` request enums, a shared `FoundationActionResult`
enum, and `FoundationDispatchError`. `Dispatch` accepts only the generated request type.
`DispatchName` and `DispatchKey` add a dynamic selector without erasing the request payload type:
an unknown selector produces `UnknownName` or `UnknownKey`, while a valid selector paired with the
wrong request variant produces `ActionMismatch`. Scoped equivalents require an explicit
`FoundationScope`. Borrowed slices and borrowed contract values cannot enter a stored dispatch
payload, and dispatch results must be owned or value types. `HasAction`, `ActionNames`, and
`ActionKeyBindings` expose the same immutable generated catalog with deterministic name and key
ordering.

An action with `@actions.Policy` makes the dispatcher require a value implementing
`foundation.actions.Authorizer`. The generated call checks every declared policy before invoking
the action and returns `Denied(policy)` on rejection. Policy names must be non-empty and unique
within one action. A returned action `Result<T, E>` maps its
error into an action-specific dispatcher variant. A fallible transient activation uses a separate
activation variant unless both failures already share the same error type. Panic is not caught or
converted.

Dispatch is synchronous and never starts work implicitly. An action may return an owned `Task<T>`
created by `spawn`. The generated result variant transfers that handle to the caller, which must
wait, supervise, or explicitly discard it. Cancellation is an ordinary transferred
`std.concurrent.Cancellation` payload when the action contract needs it; there is no implicit
context parameter. Task frames cannot borrow the application or a scoped host value. Event
publication returns Result unless the selected bus API documents
infallible in-process delivery; ignored publication remains subject to Result must-use.

Functions marked with `@web.Route` are emitted into the same `FoundationApplication`. The compiler
builds a deterministic `web.RouteTable`, generates one adapter for each concrete handler signature,
and binds `@web.Path`, `@web.Query`, `@web.Header`, `@web.Form`, `@web.Body`, and `@web.Inject`
parameters without reflection. Text sources support required `String`, optional `Option<String>`,
`bool`, and integer machine types. Handler results remain either `web.Response` or
`Result<web.Response, E>` and feed a closed generated error enum. Singleton injection reads the
application value. Scoped injection constructs one value per dispatch and shares it across the
request graph; transient injection constructs a fresh graph for each parameter. Fallible
request-local constructors map to route-specific typed activation variants. Such graphs cannot
require application-boundary constructor inputs because the adapter has no implicit boundary
value. Route syntax, binding
uniqueness, DI resolution, activation compatibility, and return types are checked before the host
is derived. A route may be a function or a task. The generated adapter starts and joins a task
route before releasing request-local values. A task may transfer one scoped or transient service,
but cannot transfer a singleton or the same request-local provider twice. Cancellation already
requested by the enclosing task reaches the joined handler. A marked stale inspection artifact
contributes declarations but not function bodies
during regeneration; ordinary compilation still checks the complete derived source.

Generic free functions marked `@web.GlobalMiddleware`, `@web.GroupMiddleware`, or
`@web.RouteMiddleware` wrap the generated dispatcher without changing its closed error type. Each
function owns a request and receives a non-escaping typed `next` function. Global middleware wraps
route lookup. Matching group middleware runs from broad prefixes to narrow prefixes, followed by
exact route middleware. The numeric order is ascending on entry and descending on completion.
Duplicate orders within one scope, empty or dynamic group prefixes, and route middleware without a
matching route are compile-time errors. The application plan records the resolved ordered chain.

Authorization metadata uses `foundation.guard` typed policy attributes. The selected host must
prove that every route installs its policy and validates external request data before calling
domain code.

A `pipeline` declares a synchronous typed transform chain. Every step is a synchronous Foundation
function with one read parameter and a `Result` return; another compatible pipeline can therefore
be a step. The first parameter type matches the pipeline input, each success type matches the next
step input, and the final success type matches the declared output. All steps share the declared
domain error type. The generated function owns intermediate values, passes read loans to following
steps, and drops each value after its last use.

`retry exponential(max = N)` permits between 1 and 1024 total attempts, including the initial
attempt. Before each retry the generated function waits for 1, 2, 4, and then successively doubled
milliseconds, capped at 1024 milliseconds. Retry is allowed only on read steps, so another attempt
cannot observe a consumed or partially edited input. Retry drops every failed owned result before
waiting. Workflow declarations are synchronous; asynchronous scheduling remains explicit through
`task`, `spawn`, and framework policies.

A `saga` requires at least one compensation and passes the same read input to every step. Each non-final step returns
`Result<void, E>` and the final step returns `Result<Output, E>`. A compensation also accepts the
saga input and returns `Result<void, E>`. When a step fails, only previously completed steps are
compensated, in reverse declaration order. The failed step's compensation does not run.
Compensations run once and do not inherit the step retry policy.

The source signature names the domain result `Result<Output, E>`. The callable saga result expands
to `Result<Output, SagaNameFailure>`, where the compiler generates these closed types:

```foundation
struct SagaNameCompensationFailure {
    Original E
    CompensationCount usize
    CompensationErrors [N]E
}

enum SagaNameFailure {
    Step(error E)
    Compensation(details SagaNameCompensationFailure)
}
```

`N` is the number of declared compensations, so failure storage is fixed and requires no dynamic
allocation. `.Step` preserves the original step error when every compensation succeeds.
`.Compensation` preserves that error plus every compensation error in reverse execution order.
Panic remains fatal and does not trigger compensation.

These declarations remain optional. A program that imports no `foundation.*` package can use the
rest of the language and standard library without an application host.

## Tests, metadata, and plugins

`test "name" { ... }` declares an independently executable test body. `foundationc test` compiles
each selected body as a separate native process, continues after a failing process, checks for live
allocations, and returns failure when any test fails. Test names are unique in a project. Tests may
carry `@target` and ordinary typed attributes. `expect(condition)`, `fail(value)`, and `pass()` come
from the SDK prelude; they are not general language builtins. Fixture setup and parameterized cases
remain library and attribute features built on the same declaration.

Attributes emit typed compile-time metadata and never run arbitrary compiler plugins. Runtime
reflection is opt-in and limited to metadata explicitly retained by the program. A dynamically
loaded plugin cannot add a type to code that is already compiled. It implements an ABI-declared
contract, publishes a versioned metadata descriptor, and is rejected before activation when its
target, contract, descriptor, or ABI is incompatible.

The native plugin entry point is `foundation_plugin_query_v1`. Query only describes the plugin.
The runtime validates the descriptor before calling its create callback, so a rejected plugin does
not own persistent host resources. The descriptor contains explicit ABI and SDK versions, target
OS and architecture, a lifecycle contract hash, a bounded UTF-8 name, and create, start, stop, and
destroy callbacks. Plugin state remains behind one opaque context. Names and callback errors are
borrowed at the C boundary and copied by the host. SDK fields identify the producing toolchain for
diagnostics; plugin compatibility does not require an exact SDK match.

`foundation.plugin.NativePlugin` owns the library, context, and lifecycle state. Start and stop are
idempotent. `Registry` starts in registration order, rolls back a failed startup in reverse order,
and stops in reverse order while preserving every typed cleanup failure. Runtime plugin types never
enter source name resolution, FIR, or compile-time metadata.

`FactoryRegistry` stores reusable `fn() own Plugin` callbacks under stable names and returns a
rejected callback to its caller. `ExecSandbox` launches an external process without a shell. Its
start and stop tasks return the still-owned sandbox, enforce explicit monotonic deadlines, accept
only a bounded JSON object containing `"ready": true`, and reap the process on every failure.
Stdout is the JSONL control channel and stderr carries process-plugin logs.

WebAssembly guests use ABI `foundation:plugin` version 1.0. The installed
`foundation/wasm_plugin.h` header fixes the required export names, host import names, status values,
and packed pointer-length representation. `WasmMetadata` validates bounded names, methods, and
declared capabilities. `WasmCapabilityPolicy` requires explicit host grants before activation and
authorizes calls only after size, name, declaration, and grant checks. `DiscoverWasmPaths`
enumerates non-directory `.wasm` entries from one directory and returns owned paths in
deterministic byte order without reading a module. The optional WAMR provider implements
guest execution, bounded byte transfer, explicit WASI resources, cancellation, module close, and
discovered-module registration without adding an engine dependency to the compiler or base
runtime.

## Foundation IR

Semantic analysis resolves every name and call, assigns a type to every expression, assigns stable
local and function IDs, and rejects invalid programs. Lowering then removes source names from value
resolution and produces typed Foundation IR. Native backends accept Foundation IR only.

FIR retains structured blocks for branches and loops. C emission introduces ordered temporaries,
so generated C preserves Foundation evaluation order even where C leaves operand order unspecified.

## Reserved product semantics

Recoverable failures are explicit `Result` values. The language has no `try`, `throw`, `fails`,
exception unwinding, or implicit propagation operator. Language revisions may extend borrow
regions and partial moves, but cannot make owner copying, implicit null owners, panic unwinding,
or fallible destructors valid.

Foundation Language 1 has no weak-reference value. Read and edit loans are transient and cannot be stored
in fields or collections. A cyclic graph that needs non-owning edges stores stable application
handles or IDs and resolves them through an explicitly owned lifetime manager. This keeps object
destruction deterministic and prevents a hidden second ownership model. Match guards and wildcard
patterns use source-order evaluation, explicit exhaustiveness, and ownership-safe payload binding.

Routes remain typed attributes. Services, actions, state machines, pipelines, sagas, and tests have
accepted declaration syntax because the compiler validates their typed graph. Scheduling,
authorization policy, transport, retries, persistence, and hosting remain Foundation package
behavior.

## Attributes and metadata

`@target` is a compiler-known declaration attribute with fixed arguments and selection semantics.
It is not a framework decorator and packages cannot redefine it.

Packages define typed metadata without adding framework keywords to the compiler:

```foundation
attribute Route(method HttpMethod, path String) targets(fn)
attribute Label(value String) targets(struct, field, parameter) repeatable
```

Uppercase attribute names are visible through qualified imports; lowercase names remain private.
Applications may target functions, constructors, structs, services, enums, contracts, methods,
actions, fields, variants, and parameters. Arguments may be positional or named, but positional
arguments cannot follow a named argument. Missing, extra, unknown, duplicate, non-constant, and
incorrectly typed arguments are compile errors. Repeated applications require `repeatable` and
retain source order.
An attribute on a `pipeline` or `saga` uses the `fn` target because the workflow is an ordinary
generated callable. Its application is retained on that workflow declaration in emitted metadata.

Attribute parameters accept every numeric machine type, `bool`, `String`, fixed arrays, value
structs, and enums whose contents obey the same rule. Constants use literals, fixed array literals,
value struct literals, and enum constructors. A struct literal used as metadata spells every field
explicitly; runtime field defaults are not executed by metadata emission. Owners, borrows, slices,
contracts, function values, recursive metadata types, local values, and function calls are
rejected. Compilation never runs user code to construct metadata.

Resolved definitions and applications are part of typed FIR. They do not affect C reachability,
object layout, generated C, or the runtime ABI. `foundationc emit-metadata` writes deterministic
JSON under the `foundation.metadata/v1` schema.

An attribute declaration cannot itself carry a package-defined attribute, and its schema
parameters cannot be attributed. `@target` may select a package-scope attribute declaration, but
it cannot target a member or parameter and never appears in the emitted metadata.

## C ABI

The C boundary uses an explicit declaration:

```foundation
extern c fn nativeAdd(left i32, right i32) i32 as foundation_native_add

@blocking
extern c fn nativeRead() String as foundation_native_read

@callback(cancel = foundation_native_cancel)
extern c fn nativeStart(&result i32) i32 as foundation_native_start

extern c fn nativeLabel(label String) bool as foundation_native_label

extern c fn FoundationDouble(value i32) i32 as foundation_double {
    value * 2
}
```

The bodyless declarations import `foundation_native_add`, `foundation_native_read`, and the native
callback start operation. The `@blocking` and `@callback` imports run only through task suspension
points. The declaration with a body exports `foundation_double`. Source names keep Foundation
case-based package visibility; ABI symbols are plain C identifiers and must be unique across the
project.
They cannot be C11 or C++20 keywords, the C `bool`, `true`, or `false` macro names, identifiers
beginning with `_`, `main`, or names beginning with `fdn_`. The `fdn_` namespace belongs to
generated code and the Foundation runtime. This keeps the same header valid in both languages.

The safe ABI surface maps fixed-width integers to the corresponding `intN_t` or `uintN_t`, `isize`
to `intptr_t`, `usize` to `size_t`, `f32` to `float`, `f64` to `double`, `bool` to C `bool`, and
`void` to C `void`. A plain read `String` parameter is represented as `const fdn_string *`. The
pointer, the `fdn_string` value, and its bytes remain borrowed for the duration of the call. Native
code cannot retain them, mutate them, or release their storage.

An `&` machine scalar, `&String`, or `&NativeStruct` parameter is an exclusive call-scoped C
pointer. A plain `NativeStruct` parameter is a read-only call-scoped pointer. The struct must have
the checked C layout described below. An `&String` callee may replace the value but must preserve
valid UTF-8, release the old owned value, use Foundation allocation for new owned storage, and never
retain the pointer.
A `String` return transfers one `fdn_string` by value. `owned = 0` denotes process-lifetime static
storage; `owned = 1` denotes storage allocated with `fdn_alloc` that the receiving side releases
exactly once with `fdn_string_drop`.

Other owned values, arrays, slices, by-value structs, enums, contracts, generic functions, and
methods cannot cross this boundary yet. The compiler rejects them before C emission. A borrowed
parameter or raw pointer may name an exported concrete struct whose complete field set has a checked
C layout. Such a struct cannot be a service, generic, or custom-drop type; every field must be
exported and must be a machine scalar, raw pointer, direct C function pointer, or another checked
C-layout struct.

`extern c fn(P) R` is the direct C function-pointer type. It has no Foundation closure environment,
is copied like a native pointer, and may appear in checked C layouts and C ABI parameters or
results. Only a named function declared with `extern c` can initialize it. A managed named function
or anonymous `fn` cannot be converted because its environment and calling contract differ.

```foundation
struct NativePoint {
    X i32
    Transform extern c fn(i32) i32
}

extern c fn FoundationIncrement(value i32) i32 as sample_increment {
    value + 1
}

extern c fn FoundationApply(point *const NativePoint, value i32) i32 as sample_apply {
    // SAFETY: the caller supplies a live, aligned pointer for this synchronous call.
    unsafe {
        const transform = (*point).Transform
        transform(value)
    }
}
```

Every imported call and exported wrapper adds a native frame to the panic trace. A C function may
call the runtime panic entry points; panic remains fatal and does not unwind C or Foundation
frames.

`foundationc emit-c-header` writes a deterministic header containing exported definitions only.
The header is C11 and has C++ linkage guards. `foundationc build` and `foundationc run` accept a
repeatable `--native` argument for C source and compatible object inputs. Native C sources receive
the generated header as `foundation_abi.h` on their include path.
`foundationc run` forwards application arguments after `--`; compiler options stay before the
separator.

A package that declares `native_library c` and `native_name` can emit its checked Package
Interface IR with `foundationc emit-pii <project> -o <file.json>`. The command requires a current
target lock and at least one exported C function. The canonical JSON contains package-relative
source locations, specialized imports and exports, ownership, ABI conventions, and locked foreign
provenance. ABI minor 1 models compiler-owned `@callback` imports with the
`foundation_reactor_v1` protocol, completion status, once lifetime, reactor context, and optional
cancel symbol. ABI minor 2 adds target-specific native links, checked nominal C layouts, and direct
C function-pointer types. ABI minor 3 records the Foundation language level. Layout values remain
pointer-only at the boundary.

`foundationc build-library <project> -o <directory> --kind static|shared` builds the same checked
interface as a native distribution bundle. LLVM is the default object backend and `--backend c`
selects the C11 backend. `--kind static --pic` emits compiler-owned objects and native C inputs as
position-independent code for later inclusion in a shared library or plugin. Precompiled object
inputs must already be position-independent. Shared builds reject the redundant `--pic` option.
The bundle contains `include/<native_name>.h`,
`include/foundation/library.h`, the platform library under `lib/`, and canonical PII under
`share/foundation/`. A shared library applies the manifest ABI major to its SONAME or platform
equivalent and exports only the checked C boundary plus the allocation and panic functions in the
small public ABI support header. Library compilation does not synthesize an executable entry point.
The manifest directive `native_link <library> [target <platform>]` declares transitive native
libraries without accepting raw linker flags. Shared builds apply entries active for the selected
target. Static consumers must apply the same requirements from the emitted PII.

`foundation/library.h` publishes Foundation library ABI 1. A stable toolchain keeps that ABI
available so a compatible precompiled library can link with future toolchains on the same platform
C ABI. Package authors still version changes to their own exported symbols and layouts. The full
source, library, and plugin guarantees are in [compatibility.md](compatibility.md).

`foundationc package export <project> -o <directory> --format <format>` derives ecosystem packages
from that checked interface. `zig`, `rust`, and `go-cgo` include a static native artifact.
`go-dynamic` includes a shared native artifact and a pure-Go loader. All four preserve the C ABI,
layout, callback, and link requirements recorded by PII.

`go-source` is a distinct source translation mode. It emits `go.mod`, one Go source file, and the
same canonical PII, but no native directory. It exports normal public Foundation functions and does
not require `native_library c`, `native_name`, or `extern c` declarations. The current accepted
subset contains scalar, String, fixed-array, read/edit slice-view, value-struct, enum,
`Option`, or `Result` parameters, results, and locals; same-package body functions; non-generic
roots plus reachable closed generic constructors, associated functions, and `self` or `&self`
instance methods; struct, array, and enum
construction; complete value-struct destructuring; field and sequence access; named function
values and anonymous functions with copy, edit, or own captures; direct and function-value calls;
closed specializations of package-internal generic functions with explicit or inferred type
arguments; local, field, and sequence-element assignment and replacement; branches; while and sequence
`for` loops; break and continue; boolean expressions; checked integer arithmetic; exhaustive
expression matches with literal payload patterns and guards; block and postfix conditional
expressions; and `Result` else handling, including `Result<void, E>`. Foundation `[N]T` maps to Go
`[N]T`; read and edit `[T]` views map to `[]T`. Array-to-view conversion evaluates its source once,
including an array returned by a call. Generated Foundation bodies mutate a view only when its
Foundation parameter is editable. Go cannot encode a read-only slice parameter, so a Go caller
must preserve that read contract. Indexing remains bounds checked by Go. Concrete enum
instantiations become nominal Go value types. Each variant has an exported constructor and an
`IsVariant` method; payload variants also have a checked `GetVariant` method. String literals,
concatenation, equality, inequality, empty tests, and UTF-8 byte length map to Go String operations.
Generated checked helpers preserve Foundation overflow and division failure behavior. Conditional
expressions evaluate the condition once and only evaluate the selected branch. Match arms and
conditional branches in this subset cannot return from the surrounding function or break or
continue one of its loops.
Foundation `fn(P) R` maps to Go `func(P) R`. A named non-generic Foundation function maps to its Go
function declaration. An anonymous function maps to a Go closure. Copy and own captures are passed
through a typed construction function, which snapshots them once when the closure is created. An
edit capture passes the address of the original binding. An editable function parameter becomes a
Go pointer unless it is a slice view; consuming parameters remain Go values after Foundation has
checked the move. Function values can be stored in translated value structs and enum payloads.
A closed generic function instance is emitted once and can be called directly or stored as a
function value. `extern c fn` values are rejected in this mode.
A reachable closed generic struct application becomes one nominal Go type. Its name includes the
concrete type arguments, its field types are substituted before emission, and nested applications
compose those names. Name collisions receive deterministic numeric suffixes. Reachable
closed types materialize every compatible exported constructor and receiver method against the
same nominal specialization.
`replace` accepts mutable locals, fields, and array or slice elements. The generated call evaluates
the replacement first and computes the target address once, then stores the new value and returns
the previous value. A panic while evaluating the replacement leaves the target and its index
unevaluated.
Compound assignments materialize their mutable place before evaluating the right-hand side, then
reuse that one address for the checked operation. A right-hand side with observable work therefore
cannot redirect the assignment target.
Value-struct destructuring evaluates the initializer once and binds every field in pattern order.
Owner destructuring is rejected because this mode cannot preserve the outer allocation transfer.
`self` maps to a Go value receiver and `&self` maps to a pointer receiver. `ctor New` becomes
`NewType`; another constructor becomes `NewTypeName`, and an associated function becomes
`TypeName`. A `$self` method is rejected because Go cannot prevent the caller from reusing the
consumed value. Custom-drop structs, raw pointers, callbacks, native imports, foreign metadata,
native links, open generic exports, tasks, actions, and
other runtime-backed FIR nodes are rejected with `FDN4120`. The command does not fall back to
`go-cgo` or `go-dynamic`
implicitly.
