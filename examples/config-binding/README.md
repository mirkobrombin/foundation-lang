# Typed configuration binding

`ServerConfig.Bind` turns named string values into a typed configuration without runtime
reflection. The example starts with explicit defaults, applies multiple named sources in order,
and shows how renamed or ignored fields affect binding. It also covers the parsing rules used for
durations, string lists, and strict JSON input.

Run it directly with:

```sh
foundationc run .
```
