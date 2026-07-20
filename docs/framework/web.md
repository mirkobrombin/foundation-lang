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

Route patterns accept literal segments, `{name}`, `{name:int}`, `{name:alpha}`,
`{name:regex(expression)}`, and a final `{*rest}` catch-all. Regex constraints use the portable
bounded syntax from `std.pattern`; registration and generated routes reject invalid expressions.
Literal branches take precedence over parameters, which take precedence over catch-all branches.
Lookup still backtracks when the most specific path has no handler for the requested method.
`Request.Param` returns a copied value without exposing the router's owned parameter storage.
Registration rejects malformed names, duplicate parameters, unknown constraints, ambiguous
parameter branches, and parameter/catch-all conflicts.

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
attributes consumed by the compiler host derivation pass. A route is a free, non-generic function
or task that returns `web.Response` or `Result<web.Response, E>`. Each parameter has exactly one
binding attribute. A task route is started and joined by its generated adapter before the request
scope closes. It is structured request work, not a detached task.

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

Generated applications support typed middleware without erasing the application error. A
middleware is a generic free function. It owns each request, receives a non-escaping `next`
function, and returns the same `Result` type. It may return a response before calling `next`,
replace the request passed to `next`, or inspect and replace the returned response or error.

```foundation
@web.GlobalMiddleware(10)
fn requestLog<E>(
    $request web.Request,
    next fn($web.Request) Result<web.Response, web.DispatchError<E>>
) Result<web.Response, web.DispatchError<E>> {
    print(request.Path)
    next($request)
}

@web.GroupMiddleware("/api", 20)
fn requireApiKey<E>(
    $request web.Request,
    next fn($web.Request) Result<web.Response, web.DispatchError<E>>
) Result<web.Response, web.DispatchError<E>> {
    var active = request
    const authorized = match active.Header("X-Api-Key") {
        None: false
        Some(value): value == "expected"
    }
    if !authorized {
        discard active
        return .Ok(web.Text(401, "missing api key"))
    }
    next($active)
}

@web.RouteMiddleware(.POST, "/api/users", 30)
fn auditCreate<E>(
    $request web.Request,
    next fn($web.Request) Result<web.Response, web.DispatchError<E>>
) Result<web.Response, web.DispatchError<E>> {
    next($request)
}
```

Global middleware wraps route lookup, so it observes `NotFound` and `MethodNotAllowed`. Matching
groups then run from the broadest prefix to the narrowest prefix. Route middleware runs last,
before the handler. Within one scope, lower `order` values are outer. Completion unwinds in the
opposite order. Duplicate orders in one scope are rejected, as are group prefixes that match no
route and route middleware that targets no exact method and path pair. The derived chain is part
of the virtual application source and never creates a project source file.

Path, query, header, and form sources accept `String`, `Option<String>`, `bool`, and every integer
machine type. Required sources produce a generated `FoundationWebBindingError` when absent.
`Option<String>` receives `.None` instead. Invalid boolean and integer text produces an explicit
`Invalid` binding error with the source and binding name. `@web.Inject()` resolves one statically
selected provider. Singletons remain shared by the application, scoped providers are constructed
once per dispatch and reused within that request, and transient providers are constructed for each
injection. A task may read a copyable injected value or take ownership of one scoped or transient
value with `$`. Singleton services cannot be transferred, and the same request-local provider
cannot be transferred through multiple parameters. Owned request values and services remain alive
in the task frame until the generated adapter joins it. Cancellation already requested by the
enclosing server task is forwarded to the handler task. Non-singleton web activation cannot use
`@di.Input()` because the request adapter has no
implicit application values. Fallible constructors in one route graph share one error type and
produce an activation-specific `FoundationWebError` variant. Every request-local value is dropped
when the adapter returns. `@web.Body()` accepts either raw `String` or a local concrete struct
marked `@bind.Bindable()`. Typed bodies require a valid JSON content type and use the same generated
`BindJSON` method as configuration binding. JSON syntax errors
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
providers, unavailable activation inputs, mixed activation error types, and unsupported return
types before deriving the host.
The derived host contains deterministic route IDs, one signature-specific adapter per route,
closed binding and handler error variants, and a `Dispatch` implementation. No host file is written
by normal `check`, `build`, `run`, or language-server operation.

The executable fixture `tests/projects/application-host-web` generates an application, converts
typed path, query, header, form, raw body, and JSON model inputs, resolves a singleton service, maps
route failures, and serves a real loopback request. It proves strict JSON failures, MIME parsing,
missing and invalid binding errors, and a real HTTP 415 response. A separate regression changes an
`i32` route parameter to `i64` and proves that an explicit inspection artifact can be replaced
without affecting normal compilation. Negative fixtures pin the web declaration and typed-body
initialization failures except the internal missing-runtime invariant.

The `application-host-web-activation` fixture proves one scoped construction per request, fresh
transient construction per injection, joined task-route execution with an owned transient, typed
constructor failures, and zero live allocations. The runtime task fixture proves cancellation
forwarding across the adapter's nested synchronous wait. The
`application-host-web-middleware` fixture proves global, nested group, and route ordering plus
global handling of an unmatched route. The
lower-level `web-server.fdn` and `web-routing.fdn` fixtures cover manual handlers, precedence,
method backtracking, integer, alpha, and portable regex constraints, catch-all capture, 404/405
selection, and registration failures.

This is not the completed `app/web` compatibility boundary. Manual-router middleware, continuous
serving, graceful shutdown, TLS, OpenAPI, and the health adapter remain pending.
