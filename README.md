<div align="center">
  <img src="assets/brand/foundation-icon.svg" width="128" alt="Foundation Lang logo">
  <h1>Foundation Lang</h1>
  <p><strong>Stop maintaining your application's structure in two places.</strong></p>
  <p>
    <a href="docs/language.md">Language reference</a> |
    <a href="docs/foundation-code-standard.md">Code standard</a> |
    <a href="docs/architecture.md">Architecture</a>
  </p>
</div>

Foundation is a compiled programming language where services, routes, and contracts are declared
and checked in source instead of split across framework metadata and generated wiring.

Foundation has no garbage collector. Ownership transfers appear at call sites, and owned values
are destroyed deterministically. Programs link the Foundation runtime for allocation, I/O, tasks,
and platform access; freestanding targets are not supported.

## Why Foundation became a language

The first Foundation lived in Go. It grew out of packages for routing, authentication, policy,
relay transport, actions, and other parts of an application that usually end up scattered across a
framework. Over several years those packages were deprecated or folded into
[`go-foundation`](https://github.com/mirkobrombin/go-foundation), then the public API was rebuilt
for v2.

That work proved the model, but it also exposed the daily cost of keeping it inside Go. Foundation
v2 carried part of the application in ordinary types and part in struct tags, string keys, marker
fields, and generated registries. A refactor could remain valid Go while leaving Foundation
metadata behind, so the analyzer, generator, and editor all had to reconstruct the same intent.

A small v2 action looked like this:

```go
type Greeter struct {
    Prefix string
}

type Greet struct {
    _       struct{} `action:"greet" keys:"ctrl+g"`
    Name    string   `json:"name"`
    Greeter *Greeter `inject:"greeter"`
}

func (a *Greet) Handle(context.Context) (any, error) {
    return a.Greeter.Prefix + a.Name, nil
}

func Build() (*app.App, error) {
    application := app.New().Provide("greeter", &Greeter{Prefix: "Hello, "})
    RegisterFoundation(application)
    _, err := application.Build()
    return application, err
}
```

`foundation generate` turned those conventions into the `RegisterFoundation` function and the
static action registry. That was useful, but every new relationship added another agreement to
maintain between source, generated code, diagnostics, and editor support. The price showed up while
changing an application, even when its runtime behavior was simple.

Foundation Lang puts the service, its constructor, and its action in the grammar:

```foundation
package example.services

import foundation.actions

service Greeter {
    prefix String

    ctor New($prefix String) {
        Greeter { prefix = prefix }
    }

    @actions.Name("greet")
    @actions.Key("ctrl+g")
    action Greet(self, name String) String {
        self.prefix + name
    }
}
```

`@actions.Name` and `@actions.Key` are typed attributes declared by the `foundation.actions`
package. The compiler checks where each attribute may appear, then validates its arguments and
repetition before retaining the resolved value in Foundation IR. They are not strings interpreted
later by a generator.

The compiler derives `FoundationApplication` and `BuildFoundationApplication` in its semantic
model. They do not come from a generated file in the project. Both derived forms are available for
inspection:

```sh
foundationc emit-app-plan examples/services -o services.application.json
foundationc emit-app-host examples/services -o services.application.fn
```

`emit-app-plan` writes the checked application graph. `emit-app-host` writes the equivalent
Foundation source on request. Neither command changes the project or creates a file required by a
normal build.

## What choosing Foundation changes

Services and actions are application declarations. Contracts and the closed workflow forms
(`state_machine`, `pipeline`, and `saga`) are language declarations too. Routes remain typed
attributes. Worker supervision and pooling are library APIs over tasks and channels.

At a call site, ownership and recoverable failure remain visible instead of being hidden by
framework behavior. Existing native libraries remain available through the checked C ABI. LLVM
object output is the default, while C11 output remains available for portable or inspectable
builds.

## Status

Foundation 1.0 is the language design target. The compiler, SDK, and package manifest remain on the
0.1 release line. There is no public binary release or compatibility promise yet.

Documentation uses three state labels. `Implemented` means the repository contains the feature and
its tests. `Optional` means the implementation exists but needs an external dependency or host
toolchain. `Specified` means the 1.0 target defines the behavior but the compiler or SDK does not
provide it yet.

| Area | State | Repository evidence |
| --- | --- | --- |
| Language and ownership model | Implemented | Compiler tests and accepted or rejected source fixtures |
| Services, actions, state machines, pipelines, sagas | Implemented | Application-plan, host, compiler, and language-server tests |
| LLVM and C11 native output | Implemented | Build, run, library, and backend test suites |
| C ABI, Zig, Rust, and Go package export | Implemented | Generated consumer and deterministic-output tests |
| OpenSSL and WAMR providers | Optional | Disabled by default; CMake enforces their dependency pins |
| Default parameter values | Specified | Defined by the 1.0 target grammar; not accepted by the current parser |
| Integrated cross-target build, run, and test | Specified | Target selection works; native commands remain host-targeted |

## Quick start

The current compiler build requires LLVM 21.1 development files exactly. It also requires CMake
3.25 or newer, Ninja, and compilers for C11 and C++20.

```sh
cmake --preset dev
cmake --build --preset dev
./build/dev/foundationc run examples/hello
ctest --preset dev
```

## Documentation

- [Language reference](docs/language.md)
- [Code standard](docs/foundation-code-standard.md)
- [Standard library](docs/stdlib/)
- [Foundation application packages](docs/framework/)
- [C and language interoperability](docs/language.md#c-abi)
- [API documentation](docs/documentation.md)
- [Compiler architecture](docs/architecture.md)

## Contributing

Use the [issue tracker](https://github.com/mirkobrombin/foundation-lang/issues) for bug reports and
language proposals. Compiler changes should include accepted or rejected fixtures for observable
language behavior.

## License

Foundation is distributed under the [MIT License](LICENSE).
