# State machine example

This project declares a closed state machine with typed transition payloads. The compiler checks
every state and transition, generates edit methods, and can render the same typed graph without
runtime reflection. Its managed machine runs a reusable pre-commit guard, ordered exit and enter
effects, lifecycle listeners, history, and timeout polling.

```sh
foundationc run examples/state-machine
foundationc emit-fsm examples/state-machine -o order.mmd --format mermaid
foundationc emit-fsm examples/state-machine -o order.dot --format graphviz --machine Order
```
