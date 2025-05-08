# Foundation package example

This project is the executable reference for package declarations, file-local imports, aliases,
same-package internal access, exported generic functions and value types, and cross-package enum
matching. It also proves that a local function value can shadow a package function. It is part of
the end-to-end compiler suite.

```sh
cmake --preset dev
cmake --build --preset dev
./build/dev/foundationc run examples/packages
```
