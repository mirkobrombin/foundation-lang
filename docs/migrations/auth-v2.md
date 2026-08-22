# Migrating Foundation v2 authentication

Foundation v2:

```go
token, err := auth.SignToken(auth.Payload{Sub: "account-42", Exp: expires}, secret)
payload, err := auth.VerifyToken(token, secret)
```

Foundation Lang:

```foundation
const key = bytes.FromText(secretText)
const token = auth.SignToken(auth.Payload {
    Sub = "account-42"
    Exp = expires
}, key) else error {
    return .Err(error)
}
const payload = auth.VerifyToken(token, key) else error {
    return .Err(error)
}
```

The token wire representation is unchanged for HMAC-SHA256. Replace `[]byte` with owned
`bytes.Bytes`, preserve the 32-byte minimum, and map sentinel checks to exhaustive `auth.Error`
matching. There is no `try`, thrown authentication exception, or untyped error comparison.

For rotation, create the signing key first and append older verification keys:

```foundation
const current = auth.NewHMACKey("2026-08", $currentSecret) else error {
    return .Err(error)
}
const previous = auth.NewHMACKey("2026-07", $previousSecret) else error {
    return .Err(error)
}
var keys = auth.NewService($current)
keys.AddKey($previous) else error {
    return .Err(error)
}
const signed = keys.Sign(auth.StandardClaims {
    Sub = "account-42"
    Exp = expires
    Iat = issuedAt
    Jti = sessionID
    Iss = "accounts"
    Aud = "foundation-api"
}) else error {
    return .Err(error)
}
```

`Service.Sign` and `Service.Verify` use `StandardClaims`. `Iat`, `Jti`, `Iss`, and `Aud` have zero
defaults and are omitted from the signed JSON when empty, matching Foundation v2. Foundation also
rejects unknown or duplicate claim fields instead of silently ignoring a misspelling.

For HTTP handlers, replace context lookup and casting with `foundation.auth.web.Protect`. The
wrapped `AuthenticatedHandler<E>` receives `auth.Payload` as a typed parameter. The current
migration target covers v2 HMAC authentication. Keep asymmetric deployments on v2 until the
corresponding Foundation providers close the compatibility row.
