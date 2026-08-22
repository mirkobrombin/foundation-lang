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
one request, and accepts bounded `Content-Length`, chunked, close-delimited, and no-body responses.
Close-delimited bodies are read as arbitrary byte chunks until clean EOF; a one-byte probe at the
configured boundary distinguishes an exact-size body from overflow. The transport rejects
duplicate framing fields, simultaneous content length and transfer encoding, unsupported transfer
codings, oversized lines, more than 128 headers, invalid status lines, and bodies beyond
`MaximumResponseSize`. A request has a 15 second deadline by default. The same monotonic deadline
is applied to connect, write, status, headers, chunks, trailers, and body reads. Informational
responses are bounded and skipped before the final response. Protocol upgrades are rejected.
HEAD and 304 responses ignore framing bodies, while 204 responses reject body framing fields.
Exactly 128 headers or trailers are accepted; the next field is rejected.

`StreamingNetwork()` returns the same validated response head without buffering the body. Its
`StreamingTransport<NetworkError>` result owns a `ResponseBody<NetworkError>`. Each `Read` consumes
that owner and restores it beside either a non-empty `BodyPart.Chunk`, final
`BodyPart.Complete(trailers)`, or `BodyReadError`. The caller chooses a positive per-read maximum;
zero returns `InvalidLimit` without consuming wire data. Content-Length, chunked, close-delimited,
HEAD, 204, and 304 framing share the same parser and absolute deadline as `Network()`. Chunked
trailers are validated, bounded to 128 fields, preserved in wire order, and returned only at clean
completion. Dropping the body closes the unread connection deterministically.

The buffered `Network()` transport is implemented by collecting that stream into one bounded
`std.bytes.Builder`. This keeps a single framing implementation while preserving the replayable
`Response` API used by middleware, retry, and redirects.

`NetworkWith` and `StreamingNetworkWith` accept one `NetworkPolicy`. The default policy retains up
to eight idle connections. A zero limit disables retention and a negative limit is rejected.
Connections are keyed by normalized origin or forward proxy. A connection returns to the pool only
after a self-delimited response is consumed and the peer explicitly confirms keep-alive. Dropping
an unfinished response stream closes the connection instead.

`NetworkPolicy.Proxy` routes plaintext requests through one explicit HTTP forward proxy. The
transport sends an absolute-form request target, keeps the origin in `Host`, strips URL credentials
and fragments, and supplies the configured `Proxy-Authorization` value only when the request does
not already provide one. HTTPS proxy tunneling is not implicit in this plaintext transport;
applications that require it install a transport with that policy explicitly.

The transport emits `Connection: keep-alive` unless the request already provides a valid field.
It validates a supplied content length against buffered bodies, rejects request transfer encoding,
and retains response order and header spelling.

Request and response bodies are owned `std.bytes.Bytes` values. `NewRequest` copies one String
into bytes, while `NewBinaryRequest` accepts arbitrary owned bytes without UTF-8 validation.
`Request.TextBody` and `Response.TextBody` return a validated text copy when a caller wants text.
The native transport writes and reads raw bytes, including NUL and invalid UTF-8. Retry and 307 or
308 redirect replay create independent byte copies before transferring an attempt to the
transport.

Streaming uploads use a separate typed source boundary:

```foundation
contract UploadPlan<E> {
    fn Length(self) u64
    fn Replayable(self) bool
    fn Open(&self) Task<Result<own UploadBody<E>, E>>
}
```

`NewUploadRequest` combines ordinary request metadata with one owned `UploadPlan<E>`. `NewUpload`
creates a serial `UploadClient<E>` over an explicit `NetworkPolicy`. Each `UploadBody<E>.Read`
returns its owner beside one non-empty byte chunk, clean completion, or the original typed source
failure. The client writes chunks directly to the socket and checks exact agreement with the
declared length. Early completion, excess bytes, and empty chunks are distinct typed failures.

An upload plan may be one-shot. The first attempt is still valid, but retry or a 307/308 redirect
that preserves the body returns `UploadError.ReplayRequired` unless `Replayable` is true. A
replayable plan is reopened for each admitted retry and body-preserving redirect. Redirects that
rewrite POST to GET continue without opening another body. This is the explicit replacement for a
hidden `GetBody` callback.

`Logging<E>` emits a structured record to `LogSink`. The logged URL excludes credentials, query,
and fragment. The sink receives status or a typed transport description and monotonic duration.

`TLSNetwork` adapts an explicitly installed `TLSProvider` to the ordinary client contract.
`OpenSSLTLS()` supplies the optional OpenSSL 3.5.6 implementation with SNI, peer-name validation,
bounded HTTP/1.1 framing, typed timeout and cancellation, and no plaintext fallback.
`OpenSSLTLSWithAuthority` uses one explicit PEM trust bundle instead of ambient trust. The plain
`Network()` transport still returns `NetworkError.TLSUnavailable` for HTTPS so selecting TLS stays
visible in application composition.
