# `std.bytes`

`std.bytes.Bytes` owns arbitrary binary data without weakening the UTF-8 invariant of `String`.
Construction copies text bytes, `Copy` creates independent storage, and `Text` validates UTF-8
before returning a String. `Len` and `At` inspect data without exposing its address. `Close` is
idempotent, and automatic destruction clears the backing allocation before release.

```foundation
import std.bytes

const value = bytes.FromText("Foundation")
const encoded = bytes.EncodeBase64URL(value) else error {
    return .Err(error)
}
const decoded = bytes.DecodeBase64URL(encoded) else error {
    return .Err(error)
}
```

Base64URL uses the canonical unpadded RFC 4648 alphabet. Decoding rejects padding, invalid
characters, impossible lengths, and nonzero unused tail bits. `HmacSha256` returns an owned
32-byte digest. `ConstantTimeEqual` compares equal-length values without a data-dependent early
exit and is the required comparison for authentication tags.

`EncodeBase64` and `DecodeBase64` provide canonical padded standard Base64 for protocols that
require it. `EncryptAES256GCM` and `DecryptAES256GCM` require a 32-byte key and use the supplied
String as associated data. Encryption returns `nonce || ciphertext || tag` with a 12-byte random
nonce and 16-byte authentication tag. Authentication failure never returns partial plaintext.

`SecretMemory` is a low-level synchronized store used by `foundation.secrets`. It copies values on
set and get, supports explicit shared ownership through `Clone`, and clears native key/value memory
on replacement, deletion, and final close. Application code should prefer the framework store.

The package does not expose its runtime handle or mutable byte storage. Native code receives
opaque handles through the Foundation runtime ABI.
