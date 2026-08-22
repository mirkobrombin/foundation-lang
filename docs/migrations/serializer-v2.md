# Migrate Foundation v2 serializer

Replace each reflection policy with typed metadata on the serialized struct:

- add `@serializer.Serializable()` to each concrete root and nested struct;
- replace `json`, custom, or selected tag names with `@serializer.Name("...")`;
- replace an excluded tag with `@serializer.Ignore()`;
- map `WithNaming` to `Policy.Naming`;
- map pointer absence and `WithIgnoreNil` to `Option<T>` and `Policy.IgnoreNone`;
- map `WithIgnoreZero` to `Policy.IgnoreZero`;
- replace every `WithCustomType` registration with a typed `@serializer.Encode` and
  `@serializer.Decode` pair on that type.

Call `value.Marshal()` or `value.MarshalWith(policy)` instead of passing `any` to a policy. Decode
through `Type.Unmarshal(source)` or `Type.UnmarshalWith(source, policy)`, which returns the typed
value instead of mutating a pointer.

Foundation ignores unknown input fields by default to preserve v2 behavior. Enable
`RejectUnknown` only when the application intentionally adopts stricter input validation. Missing
fields keep declared defaults; fields without defaults receive their language zero value.

The generated JSON wire format is compatible for the shared scalar, naming, omission, exclusion,
custom conversion, unknown-field, and invalid-assignment cases. Test persisted documents before a
cutover if they contain maps, arrays, interface values, anonymous fields, or converters whose wire
form is not covered by the shared fixture. These types are not accepted by the current derived
surface rather than being handled through runtime reflection.
