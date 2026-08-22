# `foundation.auth`

`foundation.auth` signs explicit claims with HMAC-SHA256 and returns every recoverable failure as
`Result`. There is no exception path and no implicit authentication context.

```foundation
import foundation.auth
import std.bytes

const secret = bytes.FromText("0123456789abcdef0123456789abcdef")
const token = auth.SignToken(auth.Payload {
    Sub = "account-42"
    Exp = 4102444800
}, secret) else error {
    return .Err(error)
}

const payload = auth.VerifyToken(token, secret) else error {
    return .Err(error)
}
```

The compact two-part format is byte-compatible with Foundation v2 `core/auth`: unpadded Base64URL
JSON, a dot, and an unpadded Base64URL HMAC-SHA256 signature over the raw JSON bytes. Secrets must
contain at least 32 bytes. Tokens are limited to 16 KiB, signatures use constant-time comparison,
and expiration fails at the exact expiration second.

`NewHMACKey` and `NewService` create an ordered key ring. The first key signs. `AddKey` appends a
verification key without changing the signer, rejects duplicate IDs, and `Service.Verify` selects
the encoded key ID before verification. The service signs and returns typed `StandardClaims` with
`Sub`, `Exp`, `Iat`, `Jti`, `Iss`, and `Aud`. Empty optional claims use the same omitted JSON form as
Foundation v2. Key material remains owned and is cleared on release.

`foundation.auth.web` adapts an `AuthenticatedHandler<E>` to `foundation.web.Handler`. `Protect`
owns a validated secret, accepts the Bearer scheme case-insensitively, verifies the token, and
passes a typed `Payload` to the handler. Missing headers, malformed authorization values, token
failures, and application failures remain separate `BearerError<E>` variants.

`OpenSSLAsymmetric()` provides RS256, ES256, and EdDSA through the optional OpenSSL 3.5.6 adapter.
The provider accepts explicit PEM key bytes, validates the selected key family and curve, rejects
algorithm mismatch without downgrade, and keeps OpenSSL outside the compiler and base runtime.
Applications link the adapter explicitly.
