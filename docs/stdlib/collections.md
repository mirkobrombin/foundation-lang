# `std.collections`

`std.collections` contains dynamic collections implemented in Foundation source. `List<T>` is a
singly linked general container:

```foundation
fn NewList<T>() own List<T>
fn Len(self) i32
fn IsEmpty(self) bool
fn PushFront(&self, $value T) void
fn PopFront(&self) Option<T>
fn Clear(&self) void
```

`PushFront` and `PopFront` are constant time. `Clear` is linear in the number of elements. The list
owns every node and moves elements into and out of nodes, so it supports both copy and move-only
element types. `PopFront` returns `.None` for an empty list.

Dropping a list clears its nodes iteratively before generated field cleanup. This prevents cleanup
of a long list from following a recursive C call chain. The allocation verification fixture creates
and drops 100,000 elements and requires zero live runtime allocations at process exit.

`Queue<T>` uses two owned lists to preserve FIFO order without copying elements:

```foundation
fn NewQueue<T>() own Queue<T>
fn Len(self) i32
fn IsEmpty(self) bool
fn Enqueue(&self, $value T) void
fn Dequeue(&self) Option<T>
fn Clear(&self) void
```

`Enqueue` transfers its value into the queue. `Dequeue` transfers the oldest value back to the
caller and returns `.None` when empty. Rotation is amortized constant time and cleanup remains
iterative.

This package does not provide indexed access, iteration, implicit hashing, synchronized mutation,
or a stable allocator parameter. Those policies require an explicit domain type or concurrent
owner instead of hidden collection behavior.
