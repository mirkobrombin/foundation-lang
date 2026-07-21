# Foundation Language Tour

`main.fdn` is the executable reference for the current language subset. It is part of the compiler
test suite and must change with the language. Its unused C import and callable C export keep ABI
syntax in the tour without requiring a native object to run it.
Target-selected declarations and `std.platform` keep platform branching in the same executable
reference without C preprocessor conditions. The tour also exercises read-only environment access,
checked text inspection, portable path joining, JSON parsing, UTC time formatting, and typed
package-defined attributes with metadata-safe aggregate values. It also verifies prelude UUID
parsing, canonical formatting, Nil, and process-monotonic version 7 generation.
Typed pipeline chaining, bounded retry, and reverse saga compensation are exercised by the same
executable. The worker section includes an owned closure environment qualified with
`transferable fn` and a generic function constrained by `<T transferable>`.

The complete accepted 1.0 syntax and its implementation table live in
`../../docs/language.md`. The 24-chapter newcomer tour remains in
`../../docs/foundation-syntax-newcomer-review.md`; it includes accepted target forms that stage 0
does not compile yet.

Build the compiler, then run the tour:

```sh
cmake --preset dev
cmake --build --preset dev
./build/dev/foundationc run examples/language-tour/main.fdn
./build/dev/foundationc emit-metadata examples/language-tour -o tour.metadata.json
```

In VS Code, run the `Run Foundation language tour` task from this folder.
