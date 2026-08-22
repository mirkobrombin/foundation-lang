# Migrate Foundation v2 OpenAPI metadata

Foundation v2 discovers route tags and an optional `OpenAPIMeta() map[string]any` method at
runtime. Foundation Lang reads typed route and OpenAPI attributes during application derivation.

Foundation v2:

```go
type GetUser struct {
    _  struct{} `method:"GET" path:"/users/{id:int}"`
    ID int64    `path:"id"`
}

func (*GetUser) OpenAPIMeta() map[string]any {
    return map[string]any{
        "summary": "Find a user",
        "responses": map[int]any{200: "User found", 404: "User not found"},
    }
}
```

Foundation:

```foundation
@openapi.Summary("Find a user")
@openapi.Response(200, "User found")
@openapi.Response(404, "User not found")
@web.Route(.GET, "/users/{id:int}")
fn GetUser(@web.Path("id") id i64) web.Response {
    discard id
    web.Empty(204)
}
```

Replace `openapi.Build(title, version, handlers...)` with:

```sh
foundationc emit-openapi . -o openapi.json --title "Users API" --version "1.0.0"
```

Without overrides, the package manifest supplies title and version. Do not reproduce metadata
maps, a handler list, or an `OpenAPIMeta` contract. Imported routes are discovered from the same
static graph used by the generated application.

| Foundation v2 | Foundation Lang |
| --- | --- |
| route struct tags | `@web.Route` and parameter binding attributes |
| `MetaProvider` map | typed `foundation.openapi` attributes |
| `Build(title, version, handlers...)` | `foundationc emit-openapi` |
| reflective handler validation | compile-time application diagnostics |
| constrained path normalization | the same normalization during deterministic emission |
| optional query/header in every document | required unless the Foundation type is `Option<String>` |
