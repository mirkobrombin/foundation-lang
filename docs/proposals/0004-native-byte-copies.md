# 0004: Native byte copies

Status: accepted

## User problem

Native adapters sometimes receive caller-owned buffers that cannot use Foundation runtime handles.
Copying those buffers through text or exposing managed storage would either change the bytes or
weaken the ownership boundary.

## Design

`bytes.CopyFromRaw` copies a readable native range into owned `Bytes`. `Bytes.CopyToRaw` copies the
complete value into writable caller-owned storage and rejects insufficient capacity. Both calls
require `unsafe`, and their public SAFETY contracts place pointer validity on the caller.

```foundation
unsafe {
    const value = bytes.CopyFromRaw(source, length) else return
    value.CopyToRaw(destination, capacity) else return
}
```

The runtime never retains either pointer. A zero-length copy accepts a null pointer.

## Compatibility

The Foundation APIs and runtime entry points are additive. Existing Language 1 source, native
libraries, and plugins keep their behavior. Managed byte representation remains private.

## Diagnostics

No diagnostic changes. The existing unsafe-call checks apply.

## Implementation

The C runtime validates handles, pointer and length combinations, and destination capacity before
copying. `std.bytes` maps a closed handle to `Error.Closed` and invalid ranges to
`Error.OutOfBounds`.

## Tests

The runtime test covers successful round trips, zero length, null pointers, insufficient capacity,
and cleanup. The raw-pointer fixture copies native arrays through `Bytes` on the LLVM and C
backends.

## Alternatives

Exposing a pointer into managed `Bytes` would let native code outlive or mutate the owner. Requiring
runtime handles would couple external adapters to the Foundation allocator. Explicit copies keep
the lifetime boundary local.
