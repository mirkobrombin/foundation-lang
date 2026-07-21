# Distributed methods example

This project splits one `User` type across the files that own its declaration, constructor, and
methods. The type still implements `Profile` as a single composite declaration, which makes the
example useful for both contract checks and the editor's cross-file view.

Open `app/src/main.fn`, then try:

- Hover a declaration or stop inside a call to inspect its signature and documentation.
- Type `user.` to complete fields and methods, then follow a method to the file that owns it.
- Open Type Hierarchy on `Profile` to reach `User`.
- Run `Foundation: Peek Composite Type` on `User` to browse and edit every contributing source
  file in one view.

```sh
foundationc package verify examples/distributed-methods/app
foundationc run examples/distributed-methods/app
```
