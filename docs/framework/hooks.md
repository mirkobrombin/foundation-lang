# Hooks

`foundation.hooks` coordinates typed lifecycle work around a named action. It owns callback task
factories, preserves registration order, and keeps cancellation and failure policy in the API.

## Callback contract

A hook factory has this type:

```foundation
transferable fn(
    $String,
    $concurrent.Cancellation
) Task<Result<void, E>>
```

The key and cancellation token are owned by the returned task. A callback may capture owned
transferable state. It cannot capture a borrowed local because the callback outlives registration
and may cross an executor boundary.

Use `Before` and `After` for one key. Use `BeforeAll` and `AfterAll` for every key. Global hooks run
before keyed hooks in both phases. After hooks run only when the wrapped action succeeds.

## Sequential execution

```foundation
import foundation.hooks
import std.concurrent

enum SaveError {
    Rejected
}

task auditTask(
    $key String,
    $cancel concurrent.Cancellation
) Result<void, SaveError> {
    if cancel.IsRequested() return .Err(.Rejected)
    print(key)
    .Ok
}

fn audit(
    $key String,
    $cancel concurrent.Cancellation
) Task<Result<void, SaveError>> {
    spawn auditTask($key, $cancel)
}

fn main() i32 {
    var runner = hooks.New<SaveError>(.StopOnFirstError)
    discard runner.Before("save", $audit)
    0
}
```

The action passed to `Run` is another transferable task factory that accepts a cancellation token.
`RunError<E>` distinguishes reentrancy, hook failures, and the action failure. `Failure<E>` records
the lifecycle phase.

`.BestEffort` applies inside one phase. Every matching hook in that phase runs and `.Hooks` owns
the failures in registration order. A failing before phase still prevents the action.

## Parallel execution

`RunParallel` consumes the runner and returns `Task<own ParallelRun<E>>`. The result returns the
runner after every hook task has joined:

```foundation
const pending = runner.RunParallel("refresh")
const completed = $pending.wait()
const hooks.ParallelRun { Runner Value } = completed
runner = Runner
```

All matching before and after hooks start together. A failure requests the shared cancellation
source. Hooks choose where to observe `Cancellation.IsRequested()`. The runner still joins every
task and reports failures in launch order instead of scheduler completion order.

## Timeout

`RunWithTimeout<E>` is a task. It requires a positive `time.Duration`, passes a cancellation token
to the action, and returns only after the action task has joined. `.TimedOut` therefore never means
that cleanup continues in the background.

## Discovery

Foundation does not inspect method names such as `OnEnterPaid` at runtime. Register the callable
explicitly, use a contract for a reusable lifecycle role, or use a language construct such as
`state_machine` when the lifecycle is part of a closed model. The language server already knows
the signatures and definitions, so a reflection metadata cache would duplicate compiler facts.
