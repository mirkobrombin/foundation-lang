# Foundation Language Tour

`main.fn` is both a runnable tour and a compiler fixture, so it changes whenever the accepted
language surface changes. Follow it in source order to see declarations, ownership, contracts,
generic constraints, tasks, workers, pipelines, sagas, standard packages, and native interop used
in one program.

The C import and export keep ABI syntax in the tour without requiring a native object at runtime.
Platform-specific declarations use `std.platform` rather than preprocessor conditions, while the
remaining sections use ordinary standard packages for files, paths, JSON, time, UUIDs, and process
environment access.

The complete Foundation 1.0 syntax and implementation table live in
[`docs/language.md`](../../docs/language.md).

Build the compiler, then run the tour:

```sh
cmake --preset dev
cmake --build --preset dev
./build/dev/foundationc run examples/language-tour
./build/dev/foundationc emit-metadata examples/language-tour -o tour.metadata.json
```

In VS Code, run the `Run Foundation language tour` task from this folder.
