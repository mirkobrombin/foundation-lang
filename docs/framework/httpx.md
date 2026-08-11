# `foundation.httpx`

`foundation.httpx` separates one-request transports from client policy. `Transport<E>` owns the
protocol boundary, while `Builder<E>` assembles middleware, retry, and circuit-breaker policy.
`Build` consumes the builder, so configuration cannot race with a running request.

```foundation
var builder = httpx.New<httpx.NetworkError>(httpx.Network())
builder.UseStateful(httpx.RequestID<httpx.NetworkError>(
    $"X-Request-ID"
) else error {
    return .Err(error)
})
const client = builder.Build()

var request = httpx.NewRequest(
    $"GET",
    $"http://127.0.0.1:8080/health",
    $""
) else error {
    return .Err(error)
}
request.Timeout = time.Seconds(5)

const pending = client.Do($request)
const httpx.DoOutcome { Client Value } = $pending.wait()
```

`DoOutcome<E>` returns the reusable serial client owner beside the typed result. A second call
cannot overlap accidentally because the first call transfers `Client<E>` into its task.

Requests are replayable values. Retry and redirect copy method, URL, headers, body, deadline, and
one UUIDv7 request identity. Redirect handling supports 301, 302, 303, 307, and 308. POST changes
to GET for 301, 302, and 303. Authorization, proxy authorization, and cookie headers are removed
on a cross-origin hop. Header middleware runs on each attempt but only injects its value while the
current URL retains the original origin. Origin comparison normalizes default ports. Redirect
resolution accepts absolute, scheme-relative, absolute-path, relative-path, query-only, and
fragment-only references. It removes RFC 3986 dot segments while preserving the resolved query
and fragment, and rejects malformed percent escapes, invalid ports, unbracketed IPv6 authorities,
and non-HTTP schemes.

`Network()` is the portable HTTP/1.1 transport over `std.net`. It resolves DNS, opens TCP, writes
one request, and accepts bounded `Content-Length`, chunked, and no-body responses. It rejects
duplicate framing fields, simultaneous content length and transfer encoding, unsupported transfer
codings, oversized lines, more than 128 headers, invalid status lines, and bodies beyond
`MaximumResponseSize`. A request has a 15 second deadline by default. The same monotonic deadline
is applied to connect, write, status, headers, chunks, trailers, and body reads. Informational
responses are bounded and skipped before the final response. Protocol upgrades are rejected.
HEAD and 304 responses ignore framing bodies, while 204 responses reject body framing fields.
Exactly 128 headers or trailers are accepted; the next field is rejected.

The transport always emits `Connection: close` unless the request already provides a valid field.
It validates a supplied content length against the replayable body and rejects request transfer
encoding. Response order and header spelling are retained.

Request and response bodies are owned `std.bytes.Bytes` values. `NewRequest` copies one String
into bytes, while `NewBinaryRequest` accepts arbitrary owned bytes without UTF-8 validation.
`Request.TextBody` and `Response.TextBody` return a validated text copy when a caller wants text.
The native transport writes and reads raw bytes, including NUL and invalid UTF-8. Retry and 307 or
308 redirect replay create independent byte copies before transferring an attempt to the
transport.

`Logging<E>` emits a structured record to `LogSink`. The logged URL excludes credentials, query,
and fragment. The sink receives status or a typed transport description and monotonic duration.

HTTPS currently returns `NetworkError.TLSUnavailable` before opening a plaintext connection. TLS,
certificate resolution, streaming bodies, connection pooling, and proxy support remain open and
are not claimed by this checkpoint.
