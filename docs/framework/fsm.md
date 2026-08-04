# State machines

Foundation state machines are closed value types declared with `state_machine`. The compiler owns
the state graph, validates event payload ownership, and generates one edit method per event. A
failed event returns a typed transition error and leaves the value unchanged.

```foundation
state_machine Order {
    state Draft
    state Submitted
    state Paid(receipt i32)
    state Expired

    on Submit from Draft to Submitted
    on Pay(receipt i32) from Submitted to Paid(receipt)
    on Expire from Submitted to Expired after 60.seconds
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

`foundation.fsm.Machine<S, G, TE, LE>` adds runtime guards, effects, history, ordered lifecycle
listeners, and explicit timeout polling around any typed state. `S` is the state value, `G` is the
trigger, `TE` is the transition, guard, and effect failure, and `LE` is the listener failure.

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
If the transition returns `Err`, the machine state, entry time, and history remain unchanged.
Registered guards then inspect independent source, candidate, and trigger snapshots in stable
priority and registration order. The first guard error rejects the candidate before lifecycle
publication or commit. A successful transition follows this order:

1. Run the transition against an independent candidate.
2. Run reusable guards against source, candidate, and trigger snapshots.
3. Capture independent source and destination snapshots and one wall-clock timestamp.
4. Publish `BeforeTransition` while the stored state is still the source state.
5. Commit the candidate, append history, and reset the monotonic entry time.
6. Run exit effects, publish `ExitState`, run enter effects, then publish `EnterState` and
   `AfterTransition`.

Listeners receive snapshots, not access to the live machine. `BeforeTransition` observes the pending
change but is not a guard. Effects and listeners run only after guard acceptance. Their failures
never roll back a valid transition or turn a committed operation into `Err`. `Apply` returns
`Ok(ApplyReport)` with ordered `EffectFailures` and `ListenerFailures`. `Err(ApplyError)` therefore
always means that no commit occurred.

```foundation
machine.Guard($authorize, events.Priority.High) else error {
    discard error
    return
}

machine.OnExit($saveAudit, events.Priority.Normal) else error {
    discard error
    return
}

machine.Subscribe($observe, events.Priority.Normal) else error {
    discard error
    return
}

const report = machine.Apply(.Submit, $submitTransition) else error {
    discard error
    return
}

if !report.IsClean() {
    handleCallbackFailures($report)
}
```

`New` runs every registered effect and collects its errors. `NewWithEffectPolicy` accepts
`StopOnFirstError` to stop the current effect phase at its first error and skip later effect phases.
Lifecycle listener publication still completes so observers see every committed transition.

`Snapshot` returns an independent current value. `History` returns independent records ordered from
oldest to newest, while `HistoryLen` avoids constructing that copy. A record stores cloned source,
destination, and trigger values plus the timestamp shared by all lifecycle phases.

`Elapsed` uses a monotonic clock. `CheckTimeout` rejects a zero or negative duration, returns
`Ok(Pending)` before expiry, and returns `Ok(Applied(report))` after committing the supplied typed
transition. It does not start a hidden timer or background task.

An `after` clause is a positive compile-time duration. It cannot use transition parameters or a
payload destination, and a source state accepts at most one timeout. The compiler emits the rule in
metadata and diagrams and generates a typed `TimeoutFor<Event>` associated function returning
`Option<u64>`. `None` means the supplied state is not one of that transition's sources.

```foundation
fn expiryTimeout(value Order) Option<u64> {
    Order.TimeoutForExpire(value)
}

fn expire(&value Order) Result<void, OrderTransitionError> {
    value.Expire()
}

machine.BindTimeout(.Submitted, .Expire, $expiryTimeout, $expire) else error {
    discard error
    return
}

var rules = machine.TimeoutRules()
const checked = machine.CheckTimeouts() else error {
    discard error
    return
}
```

`BindTimeout` rejects an uncovered source, an invalid duration, or an overlap visible through the
declared representative sources. A mapping must be stable and return the registered duration for
every state it covers. Compiler-generated mappings satisfy both rules and are statically disjoint.
`CheckTimeouts` rejects `OverlappingTimeout` before invoking either transition when custom mappings
cover the same current state. `TimeoutRules` returns independent state and trigger snapshots in
registration order. A successful check returns `NoRule`, `Pending`, or `Applied(report)` and uses
the same guard, effect, listener, history, and commit path as `Apply`.

## Concurrent ownership

Configure guards, effects, listeners, and timeout bindings before transferring a machine to
`NewConcurrent`. The constructor freezes that registration surface and moves the sole machine
owner into one task behind a bounded request channel. Cloneable handles send operations through
that channel, so every transition and observation is serialized against the same state and
history.

```foundation
const concurrent = fsm.NewConcurrent($machine, 64)
const handle = concurrent.Handle()

const pending = handle.Apply(.Submit, $submitTransition)
const result = $pending.wait()
```

`Snapshot`, `History`, `HistoryLen`, `Elapsed`, `TimeoutRules`, `Apply`, `CheckTimeout`, and
`CheckTimeouts` are task-returning operations. Transition functions must be `transferable fn`
values because a handle may cross a native worker boundary. They remain synchronous: a transition
cannot suspend or wait on the same handle, and it mutates only the candidate owned by the machine
task. `ConcurrentError` separates channel closure or cancellation from an `ApplyError` reported by
the machine.

Dropping the `ConcurrentMachine` cancels its owner task and closes external handles. A handle is
safe to move across worker executors when its state, trigger, and error types satisfy the ordinary
structural transfer rules.

## Migrating wildcard hooks

A wildcard such as `*->cancelled` becomes one transition over the complete closed source set:

```foundation
on Cancel from Draft, Submitted, Paid, Cancelled to Cancelled
```

Foundation does not preserve an open-ended wildcard. Adding a new state must make the compiler ask
whether cancellation applies to it instead of silently widening the graph. Include the destination
itself when the rule allowed a wildcard self-transition.

Reflection-discovered `OnExit<State>` and `OnEnter<State>` methods become explicit typed effects.
One effect may match the source or destination exhaustively, or separate callbacks may be
registered with stable priorities. The compatibility fixture expands the wildcard, runs four state
hooks including the self-transition hooks.
