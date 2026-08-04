# State machines

Foundation state machines are closed value types declared with `state_machine`. The compiler owns
the state graph, validates event payload ownership, and generates one edit method per event. A
failed event returns a typed transition error and leaves the value unchanged.

```foundation
state_machine Order {
    state Draft
    state Submitted
    state Paid(receipt i32)

    on Submit from Draft to Submitted
    on Pay(receipt i32) from Submitted to Paid(receipt)
}
```

Values select their starting state explicitly:

```foundation
var order Order = .Draft
order.Submit() else transition {
    discard transition
    return
}
```

Use `match` to inspect the current state exhaustively. Use `foundationc emit-fsm` to render the
same graph as Mermaid or Graphviz. Runtime history, listeners, typed guards, effects, and timeout
execution remain part of the `foundation.fsm` lifecycle package rather than hidden compiler
behavior.
