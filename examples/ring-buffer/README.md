# Ring buffer example

This bounded job queue makes ownership changes visible at each operation. `Push` gives an
unaccepted job back when the queue is full, `Peek` lends the next job to a callback, and `Pop`
transfers that job to the caller.

```sh
foundationc run examples/ring-buffer
```
