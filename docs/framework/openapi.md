# `foundation.openapi`

`foundation.openapi` describes the public HTTP contract already accepted by the Foundation
compiler. It does not scan runtime values or maintain a second route registry.

```foundation
import foundation.openapi
import foundation.web

@openapi.Summary("Find a user")
@openapi.Description("Finds one user by identifier.")
@openapi.Response(200, "User found")
@openapi.Response(404, "User not found")
@web.Route(.GET, "/users/{id:int}")
fn GetUser(
    @web.Path("id") id i64,
    @web.Query("role") @openapi.EnumValue("admin")
        @openapi.EnumValue("member") role Option<String>
) web.Response {
    discard id
    discard role
    web.Empty(204)
}
```

Generate the document from the package root:

```sh
foundationc emit-openapi . -o openapi.json
```

The manifest name and semantic version populate `info`. `--title` and `--version` override them
for a published API identity. The output is OpenAPI 3.0.3 JSON with stable path, operation,
parameter, enum, and response order.

The compiler derives parameter names, locations, required state, and scalar schema types from the
handler signature. Route constraints and catch-all markers are removed from the published path:
`/users/{id:int}` becomes `/users/{id}` and `/assets/{*path}` becomes `/assets/{path}`.

Operation metadata must remain on an `@web.Route` function. Parameter metadata is valid only on
path, query, and header bindings. Arbitrary documentation-only parameters are intentionally absent:
add a typed handler parameter when the request accepts a value.

Body and form request schemas, response bodies, reusable components, authentication schemes, and
server declarations are future additions to the OpenAPI surface. Their absence does not weaken
the implemented v2 `core/openapi` contract, which models path, query, and header parameters plus
response descriptions.
