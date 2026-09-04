# `std.bytes`

`std.bytes.Bytes` owns arbitrary binary data without weakening the UTF-8 invariant of `String`.
Construction copies text bytes, `Copy` creates independent storage, and `Text` validates UTF-8
before returning a String. `Len`, `At`, and `Slice` inspect or copy bounded ranges without exposing
native storage. `Close` is idempotent, and automatic destruction clears the complete backing
allocation before release.

`Random(length)` returns up to 16 MiB from the operating system cryptographic random source. It
returns `TooLarge` before allocation when the requested length exceeds that bound and
`EntropyUnavailable` when the platform source fails. Random storage follows the same clearing and
ownership rules as every other `Bytes` value.

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

`NewBuilder(limit)` creates a bounded binary builder. `WriteByte` accepts one value from 0 through
255, `Write` copies another `Bytes` value, and `Finish` consumes the builder. Growth never exceeds
the declared limit. Closing or dropping an unfinished builder clears its allocated capacity.

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

`RuntimeHandle`, `TakeRuntimeHandle`, and `ClaimRuntimeHandle` are the low-level bridge used by
first-party packages that exchange owned bytes with the runtime ABI. `RuntimeHandle` borrows one
opaque handle for a call. `TakeRuntimeHandle` consumes a `Bytes` owner and transfers its handle to
the callback. `ClaimRuntimeHandle` transfers one runtime allocation back into a `Bytes` owner.
Mutable storage is never exposed. Application code should use typed byte operations instead of
persisting or comparing handles.
