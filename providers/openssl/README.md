# OpenSSL provider

This optional adapter supplies TLS and asymmetric signing through OpenSSL 3.5.6 LTS. Applications
link it explicitly, so the Foundation runtime base has no OpenSSL dependency.

## Build and link

Configure with `-DFOUNDATION_BUILD_OPENSSL_PROVIDER=ON` to build the provider library. A Foundation
project can also compile the adapter directly:

```sh
foundationc build <project> \
    --native providers/openssl/foundation_openssl.c \
    --native-link ssl \
    --native-link crypto
```

Native library names are validated before reaching the linker. Raw linker flags are not accepted
through `--native-link`.

## Security boundary

The signing API accepts PEM keys and supports RS256, ES256, and EdDSA with Ed25519 keys. HTTPS
clients require SNI, certificate and hostname verification, an explicit deadline, and a
cooperative cancellation token. One monotonic deadline covers name resolution through the final
response read.

HTTPS servers select an exact SNI certificate from configured PEM pairs and reject unknown names;
there is no plaintext fallback. Closing a server wakes its listener, waits for work that already
acquired the handle, then releases the certificate store. The HTTP/1.1 parser bounds informational
responses and validates length, chunked trailers, and legal close-delimited framing.

## Verification builds

Set `FOUNDATION_BUILD_FUZZERS=ON` to run the native provider fixture with AddressSanitizer and
UndefinedBehaviorSanitizer. Use a separate build with `FOUNDATION_OPENSSL_TSAN=ON` for the race
fixture under ThreadSanitizer. The fixture manifest describes the temporary CA and two leaf
certificates created by the test.
