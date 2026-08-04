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
same graph as Mermaid or Graphviz.

## Managed lifecycle

`foundation.fsm.Machine<S, G, TE, LE>` adds runtime history, ordered lifecycle listeners, and
explicit timeout polling around any typed state. `S` is the state value, `G` is the trigger,
`TE` is the transition failure, and `LE` is the listener failure.

Construction requires clone functions for state and trigger values:

```foundation
var initial Order = .Draft
var machine = fsm.New<Order, OrderTrigger, OrderTransitionError, LogError>(
    $initial,
    $cloneOrder,
    $cloneTrigger
)
```

The functions make snapshot ownership explicit for values containing owned data. The machine never
assumes that a shallow copy is independent.

`Apply` clones the current state into a candidate and gives only that candidate to the transition.
If the transition returns `Err`, the machine state, entry time, and history remain unchanged. A
successful transition follows this order:

1. Capture independent source and destination snapshots and one wall-clock timestamp.
2. Publish `BeforeTransition` while the stored state is still the source state.
3. Commit the candidate, append history, and reset the monotonic entry time.
4. Publish `ExitState`, `EnterState`, and `AfterTransition` in that order.

Listeners receive snapshots, not access to the live machine. `BeforeTransition` observes the pending
change but is not a guard. A listener failure never rolls back a valid transition. The bus invokes
every listener with stable priority and registration ordering, then `Apply` returns every typed
failure in lifecycle order.

```foundation
machine.Subscribe($observe, events.Priority.Normal) else error {
    discard error
    return
}

machine.Apply(.Submit, $submitTransition) else error {
    discard error
    return
}
```

`Snapshot` returns an independent current value. `History` returns independent records ordered from
oldest to newest, while `HistoryLen` avoids constructing that copy. A record stores cloned source,
destination, and trigger values plus the timestamp shared by all lifecycle phases.

`Elapsed` uses a monotonic clock. `CheckTimeout` rejects a zero or negative duration, returns
`Ok(false)` before expiry, and applies the supplied typed transition after expiry. It does not start
a hidden timer or background task.

Typed reusable guards, enter and exit effects, declarative timeout rules, and a concurrently shared
owner-task handle remain open. Transition functions can reject a candidate today, but that does not
replace the final guard and effect APIs.
