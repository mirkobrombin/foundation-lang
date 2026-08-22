# Migrate Foundation v2 guard policies

Move static role rules from fields to the guarded struct. A v2 field exists only because Go stores
the tag there; Foundation policy metadata does not pretend that a static permission belongs to the
field value.

```foundation
@guard.Policy()
@guard.Allow("admin", "read")
@guard.Allow("admin", "write")
struct Document {
    @guard.Dynamic("view")
    PublicRole String
}
```

Replace `GetRoles() []string` on the identity with `HasRole(role String) bool`. Keep the identity ID
as `String`. Convert an integer, UUID, or external identifier when constructing the identity; the
guard no longer formats arbitrary values through reflection.

Replace a dynamic `map[id]role` with bounded `guard.Relationships`. Choose the maximum number of
pairs at construction, check each `Add` result, and transfer the owned value into the resource.
Duplicate pairs and an exhausted limit are explicit errors.

Call the generated method on the resource:

```foundation
const checked = document.Can(identity, "read")
match checked {
    Ok: showDocument(document)
    Err(error): deny($error)
}
```

Handle `NoPolicy` separately from `PermissionDenied`. The first means the operation is absent from
the compiled policy; the second means a rule exists but the identity does not satisfy it. Never
convert either result into an implicit boolean that discards the reason.

Use `GetRoles` only when the application needs the resolved set for auditing or presentation.
Authorization should call `Can`, which stops on the first matching rule and does not expose policy
storage.
