# Contracts example

The owning package defines an exported contract and its implementation. The application then uses
the same value directly and through a borrowed contract, which lets you compare static dispatch
with the vtable used by an existential contract value.

```sh
foundationc run examples/contracts/app
```
