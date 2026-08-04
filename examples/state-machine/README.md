# State machine example

This project declares a closed state machine with typed transition payloads. The compiler checks
every state and transition, generates edit methods, and can render the same typed graph without
runtime reflection.

```sh
foundationc run examples/state-machine
foundationc emit-fsm examples/state-machine -o order.mmd --format mermaid
foundationc emit-fsm examples/state-machine -o order.dot --format graphviz --machine Order
```
