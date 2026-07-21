# Runtime pipeline example

This example builds a reusable `foundation.pipeline` chain around a terminal handler. One
middleware is a function and the other owns state, so the source shows both registration forms.
Registration order determines nesting: the first middleware enters first and leaves last.

Run it from the repository root:

```sh
./build/dev/foundationc run examples/runtime-pipeline
```

`Then` consumes the builder and produces a pipeline that already has its terminal handler. Inputs,
outputs, and recoverable failures therefore keep their types through the complete call.
