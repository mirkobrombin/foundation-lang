# Caching example

The example stores owned values in a bounded in-memory cache and uses the serializer package when a
byte representation is needed. Read the calls around insertion and retrieval to see where cloning,
expiry, and codec failures become visible to the caller.

```sh
foundation run examples/caching
```
