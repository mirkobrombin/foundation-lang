# Foundation Language Tour

`main.fdn` is the executable reference for the current language subset. It is part of the compiler
test suite and must change with the language. Its unused C import and callable C export keep ABI
syntax in the tour without requiring a native object to run it.
Target-selected declarations and `std.platform` keep platform branching in the same executable
reference without C preprocessor conditions. The tour also exercises read-only environment access,
checked text inspection, portable path joining, JSON parsing, and UTC time formatting.

Build the compiler, then run the tour:

```sh
cmake --preset dev
cmake --build --preset dev
./build/dev/foundationc run examples/language-tour/main.fdn
```

In VS Code, run the `Run Foundation language tour` task from this folder.
