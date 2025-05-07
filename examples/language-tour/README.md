# Foundation Language Tour

`main.fdn` is the executable reference for the current language subset. It is part of the compiler
test suite and must change with the language.

Build the compiler, then run the tour:

```sh
cmake --preset dev
cmake --build --preset dev
./build/dev/foundationc run examples/language-tour/main.fdn
```

In VS Code, run the `Run Foundation language tour` task from this folder.
