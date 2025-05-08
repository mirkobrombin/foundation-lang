# Contracts example

This project defines an exported contract and implementation in one package, then borrows the
implementation through the contract from another package. The generated C uses static dispatch
for direct method calls and a vtable for borrowed contract values.

```sh
foundationc run examples/contracts
```
