# Migrating hooks from Foundation v2

## Replace erased callbacks

Foundation v2:

```go
runner.Before("save", func(ctx context.Context, key string, args []any) error {
    record(args[0].(User))
    return nil
})
```

Foundation:

```foundation
fn audit(
    $key String,
    $cancel concurrent.Cancellation
) Task<Result<void, SaveError>> {
    spawn auditTask($key, $cancel)
}

discard runner.Before("save", $audit)
```

Move typed input into an owned callback capture or an injected service. Do not reproduce `[]any`
with a generic payload container.

## Handle results

`Before`, `After`, `BeforeAll`, `AfterAll`, and `Clear` return
`Result<void, RegistrationError>`. `Run` returns `Result<void, RunError<E>>`. Hook and action
failures cannot disappear through an ignored Go `error`.

## Reuse after parallel execution

Foundation v2 keeps a shared pointer and mutates maps around goroutines. Foundation transfers the
runner to one task and returns it after join:

```foundation
const pending = runner.RunParallel("save")
const completed = $pending.wait()
const hooks.ParallelRun { Runner Value } = completed
runner = Runner
```

Using `runner` before that ownership returns is a compile-time error.

## Replace discovery

Remove `Discovery`, `MethodInfo`, method-name prefixes, and reflective `Call`. Register functions
directly or express the lifecycle as a contract or `state_machine`. This keeps callable identity,
types, ownership, and navigation in compiler metadata.

## Replace timeout wrappers

Spawn `hooks.RunWithTimeout<E>` and wait for its result. A timeout requests typed cancellation and
joins the action. Code that ignored `ctx.Done()` still delays completion, but it cannot outlive the
reported result.
