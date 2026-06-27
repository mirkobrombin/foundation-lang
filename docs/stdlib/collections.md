# `std.collections`

`std.collections` contains dynamic collections implemented in Foundation source. The current API is
the singly linked `List<T>`:

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

This package does not yet provide indexed access, iteration, tail insertion, or a stable allocator
parameter. Those operations require later language and standard-library decisions.
