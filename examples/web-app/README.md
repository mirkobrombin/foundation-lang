# Foundation web application

This example is a complete Foundation web application whose routes and service graph come directly
from typed declarations. Its handlers cover the common request inputs and response forms, while
the application combines global response middleware with an API-key-protected route group.

Run the in-process smoke test first. It exercises the same route table without opening a socket:

```sh
foundationc run examples/web-app
```

Start the server on `127.0.0.1:8080`:

```sh
foundationc run examples/web-app -- --serve
```

The server handles one connection at a time through structured tasks. These requests cover the
public health route, the protected query route, a body-bound update, and form binding:

```sh
curl http://127.0.0.1:8080/health
curl -H 'X-Api-Key: foundation-demo' -H 'X-Trace: local' 'http://127.0.0.1:8080/users/Ada?limit=10&note=demo'
curl -X POST -H 'X-Api-Key: foundation-demo' -H 'X-Enabled: true' --data 'profile body' http://127.0.0.1:8080/users/Ada
curl -X POST --data 'username=Ada+Lovelace' http://127.0.0.1:8080/login
```

Routes, binding adapters, validation, and the application host are derived in memory. No generated
Foundation source is stored with the example, so editor navigation leads back to the declarations
you wrote.

Generate the matching OpenAPI 3.0.3 document with:

```sh
foundationc emit-openapi examples/web-app -o web.openapi.json
```
