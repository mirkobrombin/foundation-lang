# Foundation web application

This example is an executable Foundation web application with compiler-generated routing and
dependency injection. It covers singleton, request-scoped, and transient services, typed path and
query values, an optional query, headers, request bodies, forms, typed handler failures, JSON and
text responses, global response middleware, group API-key middleware, and the HTTP/1.1 server.

Run its deterministic in-process smoke test:

```sh
foundationc run examples/web-app
```

Start the server on `127.0.0.1:8080`:

```sh
foundationc run examples/web-app -- --serve
```

The server handles one connection at a time through structured tasks. Try these requests:

```sh
curl http://127.0.0.1:8080/health
curl -H 'X-Api-Key: foundation-demo' -H 'X-Trace: local' 'http://127.0.0.1:8080/users/Ada?limit=10&note=demo'
curl -X POST -H 'X-Api-Key: foundation-demo' -H 'X-Enabled: true' --data 'profile body' http://127.0.0.1:8080/users/Ada
curl -X POST --data 'username=Ada+Lovelace' http://127.0.0.1:8080/login
```

Routes, binding adapters, validation, and the application host are derived by the compiler. No
derived Foundation source is stored in the example; the language server and C11 backend consume
the same semantic graph.
