# Migrate Foundation v2 scheduler state

Foundation v2 and Foundation Lang intentionally use different record encodings. V2 stores
`last_run` as RFC 3339 text and `last_latency` as Go duration text. Foundation stores Unix seconds
and latency nanoseconds. Do not point both runtimes at the same directory.

For each v2 JSON record:

1. preserve `name`, `cron`, and `last_status`;
2. parse `last_run` as UTC and write its Unix seconds;
3. parse `last_latency` as a Go duration and write its nanoseconds;
4. create a Foundation `JobRecord` and call `JobStore.Save` in a new private directory;
5. load and compare every migrated record before switching the application.

```foundation
const saving = store.Save(scheduler.JobRecord {
    Name = importedName
    Cron = importedCron
    LastRun = time.FromUnix(importedUnixSeconds)
    LastStatus = importedStatus
    LastLatency = time.Nanoseconds(importedLatencyNanoseconds)
})
const saved = $saving.wait()
```

The import boundary must reject negative latency, out-of-range timestamps, invalid names, and
records above 1 MiB. Keep the v2 directory unchanged until the Foundation fixture has registered
every migrated job and confirmed that no job runs twice in its restored minute.
