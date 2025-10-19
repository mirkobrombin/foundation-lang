# Distributed methods example

This project keeps one `User` type, its constructor, and its methods in separate files within the
owning package. It is the executable fixture for method aggregation and compiler-backed editor
features.

Open `app/main.fdn`, then try:

- Hover `NewUser`, `Rename`, `AddScore`, and `User` for declarations and documentation.
- Place the cursor inside a call to see the active parameter and its documentation.
- Type `user.` for fields and methods with documented completion items.
- Ctrl+click a method to open the source file that owns it.
- Run `Foundation: Open Composite Type View` on `User` to edit its distributed declaration.

```sh
foundationc run examples/distributed-methods
```
