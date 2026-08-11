# `foundation.guard`

`foundation.guard` derives authorization from typed resource metadata. Policies are compiled into
ordinary methods; production code does not parse tags, inspect fields, or populate a global policy
cache.

```foundation
import foundation.guard

@guard.Policy()
@guard.Allow("admin", "read")
@guard.Allow("admin", "write")
struct Document {
    @guard.Dynamic("view")
    PublicRole String

    @guard.Dynamic("manage")
    Members own guard.Relationships
}
```

`@guard.Allow` grants one named role one operation. Repeat it when a role has several operations.
The role `"*"` accepts every valid identity. The operation `"*"` applies the rule to every requested
operation. Empty and duplicate rules are compile errors.

`@guard.Dynamic` accepts either `String` or `guard.Relationships`. A `String` field names a role
selected by the resource value. `Relationships` stores bounded identity-to-role pairs and derives
the roles for `Identity.ID`. A dynamic rule grants access only when the derived role is also held by
the identity. Empty runtime role fields never grant access.

Relationship authorization checks both sides at use time: the resource must map the exact
`Identity.ID` to a role, and `Identity.HasRole` must confirm that role is still held. A stale or
tampered resource relationship therefore cannot grant a role by itself.

Every identity implements the complete contract:

```foundation
contract Identity {
    fn ID(self) String
    fn HasRole(self, role String) bool
}
```

The compiler adds `Can` and `GetRoles` to every concrete `@guard.Policy` struct. `Can` returns
`NoPolicy` when no rule covers the operation and `PermissionDenied` when a covering rule exists but
does not match. `GetRoles` returns a deterministic `guard.Roles` set: static roles are included only
when held by the identity, dynamic `String` fields contribute their configured role, and dynamic
relationships contribute roles selected by the exact identity ID. `GetRoles` reports resolved
policy data; authorization still calls `Can` to require current role membership.

`Roles` preserves first-seen order, removes duplicates, and accepts up to 65,536 roles regardless
of whether it was created directly or returned by a relationship lookup. `Relationships` preserves
insertion order, rejects empty names and duplicate pairs, and requires an explicit entry limit
between 1 and 65,536. Both values own their native storage, close exactly once, and release it
automatically on drop. Both values may move to another Foundation executor; the transfer consumes
the sole owner and does not create a shared alias.

Generated methods remain virtual compiler overlays. No generated Foundation source is written into
the project, and editor navigation returns to the policy struct that caused each method to exist.
