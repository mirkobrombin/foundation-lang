# Generated web application

This example is an executable Foundation web application with compiler-generated routing and
dependency injection. It covers a singleton service, typed path and query values, an optional
query, headers, request bodies, forms, typed handler failures, JSON and text responses, and the
HTTP/1.1 server.

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
curl -H 'X-Trace: local' 'http://127.0.0.1:8080/users/Ada?limit=10&note=demo'
curl -X POST -H 'X-Enabled: true' --data 'profile body' http://127.0.0.1:8080/users/Ada
curl -X POST --data 'username=Ada+Lovelace' http://127.0.0.1:8080/login
```

Regenerate the ordinary Foundation host source after changing routes or services:

```sh
foundationc emit-app-host examples/web-app -o examples/web-app/src/zz_foundation.fdn
```

The generated source is checked in so navigation, hover, generated API discovery, and C11
lowering all see the same application graph.
