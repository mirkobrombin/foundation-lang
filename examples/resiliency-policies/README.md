# Typed resiliency policies

This example runs the core resiliency policies while keeping their effects in the function types.
The operation error survives a retry, circuit state is explicit, and admission to the bulkhead is
represented by an owned permit whose drop releases the slot. The rate limiter waits cooperatively
instead of blocking an executor thread.

```sh
../../build/dev/foundationc run .
```
