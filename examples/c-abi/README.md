# C ABI example

This project exercises both directions of the stable native boundary. Foundation imports
`foundation_native_sum` and `foundation_native_drive`, while the C driver includes the generated
header and calls functions exported by the Foundation package. Together those calls cover the
machine scalar types, a read-only raw pointer, and a borrowed `fdn_string`.

Build and run with two native source inputs:

```sh
./build/dev/foundationc run examples/c-abi \
    --native examples/c-abi/native/native.c \
    --native examples/c-abi/native/support.c
```

Emit the public header separately:

```sh
./build/dev/foundationc emit-c-header examples/c-abi -o out/foundation_abi.h
```

The header contains only the public exports. C symbols imported by Foundation remain implementation
dependencies, and native sources passed to `foundationc` can include the generated file as
`foundation_abi.h`.
