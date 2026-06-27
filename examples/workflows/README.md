# Typed workflows

This project places the same domain operation in a transform pipeline and in a compensating saga.
Step signatures and retry bounds are checked before execution, while a failed saga returns one
closed error value containing the original domain failure and any failure raised during
compensation.

```sh
foundationc run examples/workflows
foundationc emit-metadata examples/workflows -o workflows.metadata.json
```
