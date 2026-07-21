# Serializer example

This example derives the JSON representation of `Profile` from typed serializer attributes. The
source shows an exact field name, snake-case defaults, an excluded field, and omission of `None`;
encoding and decoding need neither runtime reflection nor a generated Foundation file.

```sh
foundationc run examples/serializer
```
