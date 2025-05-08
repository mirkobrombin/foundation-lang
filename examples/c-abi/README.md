# C ABI example

This project exercises both directions of the stable native boundary. Foundation calls
`foundation_native_sum` and `foundation_native_drive`. The C driver calls the exported
`foundation_double`, `foundation_ready`, `foundation_accept_label`, and `foundation_mark`
functions through the generated header. The round trip covers scalar values and a borrowed
`fdn_string`.

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

The header contains exports only. Imported C symbols remain implementation dependencies. A native
source compiled by `foundationc` may include the generated file as `foundation_abi.h`.
