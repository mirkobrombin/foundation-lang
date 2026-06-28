# `foundation.web`

`foundation.web` is the typed HTTP boundary for Foundation applications. The package does not use
runtime reflection or erase handler values to an untyped payload.

```foundation
enum AppError {
    Failed
}

struct HelloHandler implements web.Handler<AppError> {}

methods HelloHandler {
    fn Handle(self, $request web.Request) Result<web.Response, AppError> {
        discard request
        .Ok(web.Text(200, "hello"))
    }
}
```

`Server<E>` owns its TCP listener and one `Application<E>`. A manual `Router<E>` implements that
contract and transfers each owned `Handler<E>` through `Map`. A generated
`FoundationApplication` implements the same contract while owning the static DI graph and one
handler-free `RouteTable`. Both paths preserve the typed handler error `E` without converting it to
a string or exception.

Route patterns accept literal segments, `{name}`, `{name:int}`, `{name:alpha}`, and a final
`{*rest}` catch-all. Literal branches take precedence over parameters, which take precedence over
catch-all branches. Lookup still backtracks when the most specific path has no handler for the
requested method. `Request.Param` returns a copied value without exposing the router's owned
parameter storage. Registration rejects malformed names, duplicate parameters, unknown
constraints, ambiguous parameter branches, and parameter/catch-all conflicts. `regex(...)` is
recognized but rejected until the standard library has a portable regex contract.

`ServeOne` accepts one connection, parses one HTTP/1.1 request, dispatches it, writes a response,
closes the connection, and returns the still-owned server in `ServeOutcome<E>`. Request lines and
header lines are capped at 8 KiB and requests are capped at 100 headers. Header names are compared
as ASCII case-insensitive values where protocol semantics require it. A `Content-Length` body is
read exactly and capped at 1 MiB. Duplicate or malformed lengths, oversized bodies, and transfer
encoding are rejected before dispatch. Closing the stream before the declared body length produces
`BodyTruncated` instead of reaching a handler with partial input.

`Request.Param`, `Request.Query`, `Request.Header`, and `Request.Form` return copied values without
exposing owned request storage. Query and form lookup use the first matching key, decode `+` and
percent-encoded UTF-8, and ignore malformed unrelated keys. Header lookup is ASCII
case-insensitive and returns the first field value. `Request.Body` contains the bounded raw UTF-8
body. `Request.IsJSON` parses the media type instead of matching a prefix. It accepts
`application/json`, structured `+json` subtypes, and valid MIME parameters case-insensitively. A
malformed parameter list is not a JSON content type.

`@Route`, `@Path`, `@Query`, `@Header`, `@Form`, `@Body`, and `@Inject` are typed package
attributes consumed by `foundationc emit-app-host`. A route is a free, non-generic synchronous
function that returns `web.Response` or `Result<web.Response, E>`. Each parameter has exactly one
binding attribute.

```foundation
@web.Route(.POST, "/users/{id:alpha}")
fn CreateUser(
    @web.Path("id") id String,
    @web.Query("limit") limit i32,
    @web.Query("note") $note Option<String>,
    @web.Header("X-Enabled") enabled bool,
    @web.Body() body String,
    @web.Inject() users UserStore
) Result<web.Response, RouteError> {
    .Ok(web.Text(201, "created"))
}
```

Path, query, header, and form sources accept `String`, `Option<String>`, `bool`, and every integer
machine type. Required sources produce a generated `FoundationWebBindingError` when absent.
`Option<String>` receives `.None` instead. Invalid boolean and integer text produces an explicit
`Invalid` binding error with the source and binding name. `@web.Body()` accepts either raw `String`
or a local concrete struct marked `@bind.Bindable()`. Typed bodies require a valid JSON content
type and use the same generated `BindJSON` method as configuration binding. JSON syntax errors
retain their parser kind and offset. Duplicate keys, trailing values, wrong JSON shapes, wrong
field types, and unknown fields are rejected. The host initializes generated binding fields with
their typed zero value and preserves declared field defaults. An ignored or private field without
a source default is rejected because the host cannot invent its application value. Body binding
cannot be repeated or combined with form binding. `@web.Inject()` resolves the unique singleton
provider for the parameter type from the same generated application graph used by every route.

```foundation
@bind.Bindable()
struct ProfileInput {
    @bind.JsonName("display_name")
    DisplayName String
    Age u8
    Enabled bool = true
}

@web.Route(.POST, "/profiles")
fn CreateProfile(@web.Body() $body ProfileInput) web.Response {
    discard body
    web.Text(201, "created")
}
```

Direct `Dispatch` calls preserve generated `Binding`, `JSON`, and route execution error variants.
At the transport boundary, `Application.ErrorResponse` maps missing, invalid, and JSON binding
failures to 400 and unsupported media types to 415. Its default implementation returns the handler
error unchanged, so manual routers and application-specific failures are not hidden.

A typed body marked `@validation.Validatable()` runs its generated `Validate` method after JSON
binding and before route execution. Direct dispatch adds a typed `Validation(errors)` variant and
preserves every structured error. The generated HTTP policy consumes the collection and returns
422 without exposing field values in the response body. See `docs/framework/validation.md` for the
rule set and its remaining compatibility gaps.

The compiler rejects invalid paths, exact duplicate routes, ambiguous parameter branches,
unmatched path parameters, repeated sources, unsupported binding types, missing or ambiguous DI
providers, non-singleton route injection, and unsupported return types before writing the host.
The generated host contains deterministic route IDs, one signature-specific adapter per route,
closed binding and handler error variants, and a `Dispatch` implementation. Regeneration treats an
older marked host as declarations only, so a changed route signature can replace its stale adapter
without deleting the generated file first. Normal `check`, `build`, and `run` remain strict.

The executable fixture `tests/projects/application-host-web` generates an application, converts
typed path, query, header, form, raw body, and JSON model inputs, resolves a singleton service, maps
route failures, and serves a real loopback request. It proves strict JSON failures, MIME parsing,
missing and invalid binding errors, and a real HTTP 415 response. A separate regression changes an
`i32` route parameter to `i64` while leaving the old generated host in place, then regenerates,
checks, and runs the application. Negative fixtures pin the web declaration and typed-body
initialization failures except the internal missing-runtime invariant.

The lower-level `web-server.fdn` and `web-routing.fdn` fixtures cover manual handlers, precedence,
method backtracking, integer and alpha constraints, catch-all capture, 404/405 selection, and
registration failures.

This is not the completed `app/web` compatibility boundary. Scoped route activation, asynchronous
route functions, regex constraints, middleware, continuous serving, graceful shutdown, TLS,
OpenAPI, and the health adapter remain pending.
