# Services and actions

This project turns a `Greeter` service into an action exposed by the generated
`FoundationApplication`. The constructor receives the application's greeting prefix, while the
declaration gives the service a singleton lifetime and the action its public name, key binding, and
policy. Constructor types are enough to distinguish service dependencies from application inputs,
so no injection attribute is needed there.

```sh
foundationc run examples/services
foundationc emit-metadata examples/services -o services.metadata.json
foundationc emit-app-plan examples/services -o services.application.json
foundationc emit-app-host examples/services -o services.application.fn
```

The application plan and typed host are derived in memory. `emit-app-host` writes the equivalent
Foundation source only for inspection. The editor and both native output paths read the same model,
so the project does not carry a generated Foundation file.
