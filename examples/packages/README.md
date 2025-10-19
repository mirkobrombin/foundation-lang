# Foundation package example

This project is the runnable package-system reference. The application depends on local packages
through locked paths, imports them per file, and uses both public declarations and same-package
internal declarations. The source also covers aliases, generic exports, cross-package enum matches,
and local shadowing of a package function. Each package keeps the lock for its own target.

```sh
cmake --preset dev
cmake --build --preset dev
./build/dev/foundationc package verify examples/packages/app
./build/dev/foundationc run examples/packages/app
```
