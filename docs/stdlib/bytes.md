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

The package does not expose its runtime handle or mutable byte storage. Native code receives
opaque handles through the Foundation runtime ABI.
