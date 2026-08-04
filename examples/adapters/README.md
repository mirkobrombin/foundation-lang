# Adapters example

Use this project to follow the lifetime of values stored in a typed adapter registry. It registers
and removes adapters while ordered task callbacks observe only committed changes, then selects a
default adapter and retrieves an independently owned value.

```sh
foundation run examples/adapters
```
