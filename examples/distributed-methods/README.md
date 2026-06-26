# Distributed methods example

This project keeps the `Profile` contract, one implementing `User` type, its constructor, and its
methods in separate files within the owning package. It is the executable fixture for contract
conformance, method aggregation, and compiler-backed editor features.

Open `app/src/main.fdn`, then try:

- Hover `NewUser`, `Rename`, `AddScore`, and `User` for declarations and documentation.
- Place the cursor inside a call to see the active parameter and its documentation.
- Type `user.` for fields and methods with documented completion items.
- Ctrl+click a method to open the source file that owns it.
- Open Type Hierarchy on `Profile` or Go to Implementations to reach `User`.
- Run `Foundation: Peek Composite Type` on `User` to browse and edit every source declaration
  without leaving the current editor.

```sh
foundationc package verify examples/distributed-methods/app
foundationc run examples/distributed-methods/app
```
