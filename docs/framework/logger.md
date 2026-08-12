# Logger

`foundation.logger` writes deterministic structured text to standard output. It keeps threshold
selection and field ownership visible in the calling code.

```foundation
import foundation.logger

var log = logger.NewWith(.Info)
log.Bind(logger.Field { Key = "service" Value = "catalog" })
const fields = logger.WithField(logger.Fields(), "request", "r-42")
log.Info("served", $fields)
```

`Log`, `Debug`, `Info`, `Warn`, and `Error` consume their field list. A message below the configured
level produces no output. `Bind` retains fields on the owned logger for subsequent entries.

Custom fallible output adapters are intentionally outside this initial slice. They require an
explicit transport contract and error policy instead of silently ignored sink failures.
