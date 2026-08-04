# State machine example

This project declares an order lifecycle as a closed state machine whose transitions carry typed
payloads. The declaration is used both for executable edit methods and for the Mermaid or Graphviz
graph emitted by `foundationc`, without inspecting the machine through runtime reflection.

The managed machine shows where policy attaches to that graph. A guard runs before a transition is
committed, exit and enter effects follow declaration order, and listeners can inspect history. The
`Expire` transition owns its `60.seconds` timeout rule, which the runtime polls against a monotonic
clock.

```sh
foundationc run examples/state-machine
foundationc emit-fsm examples/state-machine -o order.mmd --format mermaid
foundationc emit-fsm examples/state-machine -o order.dot --format graphviz --machine Order
```
