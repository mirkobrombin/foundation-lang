# `foundation.metrics`

`foundation.metrics` supplies local counter, gauge, histogram, and timer values. They are mutable
owned state, not globally synchronized registries.

```foundation
import foundation.metrics
import std.collections

var counter = metrics.NewCounter()
counter.Inc()

var bounds = collections.NewList<f64>()
bounds.PushFront(0.5)
bounds.PushFront(1.0)
var histogram = metrics.NewHistogram($bounds)
histogram.Observe(0.7)
```

`NewHistogram` sorts its transferred bounds. `Snapshot` returns independent bucket counts, total
count, sum, and overflow count. `Timer.Start` returns one `Timing`; `Stop` consumes it and returns a
monotonic duration or `TimerError`.
