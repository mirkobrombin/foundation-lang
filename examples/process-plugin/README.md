# Process plugin example

This project gives a plugin its own process without passing the command through a shell. The host
sends literal arguments and exchanges a bounded JSONL control protocol with the child, applying a
deadline to startup and shutdown before it reaps the process.

Build the worker from the repository root:

```sh
cmake -S examples/process-plugin/worker -B build/process-plugin
cmake --build build/process-plugin
```

Run the Foundation application on Linux or macOS:

```sh
./build/dev/foundationc run examples/process-plugin -- \
    build/process-plugin/greeter_process
```

On Windows, pass `build/process-plugin/greeter_process.exe`. The checked-in lock targets Linux.
Generate the target lock before running on another host:

```sh
foundationc package resolve examples/process-plugin --target macos
foundationc package resolve examples/process-plugin --target windows
```

Run only the resolve command for the current host. Standard output belongs to the control protocol,
so the plugin writes application logs to standard error.
