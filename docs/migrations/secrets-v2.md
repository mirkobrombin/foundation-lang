# Migrate Foundation v2 secrets

The in-memory and environment stores require no persisted migration. Replace Go byte slices with
owned `std.bytes.Bytes`, observe each task result, and clone a store when another owner must outlive
the current value.

For `CipherStore`, preserve the exact 32-byte AES key and migrate values through the public stores:

1. read each plaintext value with the v2 wrapper;
2. create the Foundation wrapper with the same key bytes;
3. set the plaintext through Foundation so it writes the Foundation nonce and authentication-tag
   layout;
4. read every value through Foundation before removing the v2 copy.

Do not copy encrypted backend bytes directly. Both implementations use AES-GCM, but their stored
envelopes are not a persistence contract.

For Vault, implement `secrets.VaultTransport` with the selected HTTP client. Map `Method`,
`Address`, `Path`, `Token`, and `Body` exactly; disable redirects and stop the response body at
`MaximumResponseSize`. The same KV v2 mount and keys remain valid because the wire value uses
standard padded Base64 in both implementations.

`PrivateFileStore` is Foundation-specific. Its HMAC filenames and encrypted file format are private
to the package. Populate it through `Set`; do not generate files outside the API.
