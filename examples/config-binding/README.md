# Typed configuration binding

This example preserves explicit defaults while applying named string values through a generated,
typed `ServerConfig.Bind` method. It demonstrates a renamed key, signed duration parsing, string
list append, named source precedence, an empty-source default, strict JSON decoding, and an ignored
field without runtime reflection.

Regenerate and run it with:

```sh
foundationc emit-app-host . -o src/zz_foundation.fdn
foundationc run .
```
