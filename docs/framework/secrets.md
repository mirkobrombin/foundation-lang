# `foundation.secrets`

`foundation.secrets` keeps stored values as owned bytes across its storage and encryption
boundaries. Every operation returns a task and an explicit `Result`; the package has no
recoverable exception path.

```foundation
import foundation.secrets
import std.bytes

const memory = secrets.NewMemoryStore()
const encrypted = secrets.NewCipherStore(
    memory.Clone(),
    bytes.FromText("0123456789abcdef0123456789abcdef")
) else error {
    return .Err(error)
}

const storing = encrypted.Set("token", bytes.FromText("secret"))
const stored = $storing.wait()
```

`Store.Clone` creates another owner for the same backend. `MemoryStore.Share` preserves the concrete
transferable type for work sent to a native worker pool. `Set` consumes the supplied key and value,
`Get` returns an independent byte owner, and deleting a missing key succeeds for writable stores.
`MemoryStore` is safe across native tasks and clears its native storage when the final owner closes.

## Composition

`NewPrefixStore` isolates a namespace without changing the wrapped backend. `NewFallbackStore`
tries the secondary store only for expected absence or read-only failures; transport, protocol,
permission, and authentication failures stay visible. `NewCipherStore` encrypts values before they
reach its wrapped store and binds each ciphertext to its logical key.

`NewPrivateFileStore` creates an owner-private directory and stores authenticated ciphertext under
HMAC-derived filenames. The key is supplied by the application; the package does not invent an
ambient machine key or expose plaintext fallback files.

## Vault

```foundation
contract AppVaultTransport implements secrets.VaultTransport {
    fn Clone(self) own secrets.VaultTransport
    fn Send(
        self,
        $request secrets.VaultRequest
    ) Task<Result<secrets.VaultResponse, secrets.Error>>
}
```

Create a store with `NewVaultStore` or read `VAULT_ADDR` and `VAULT_TOKEN` with
`NewVaultStoreFromEnvironment`. The transport implementation must not follow redirects and must
stop reading at `MaximumResponseSize`. `VaultStore` builds KV v2 paths, sends the token through the
dedicated request field, limits secrets to 512 KiB, and limits responses to 1 MiB. Remote addresses
must use HTTPS. Plain HTTP is accepted only for canonical `localhost`, `127.0.0.1`, or `[::1]`
addresses with an optional decimal port from 1 through 65535.
