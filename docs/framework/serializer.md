# `foundation.serializer`

`foundation.serializer` derives typed JSON codecs from concrete structs. Mark the struct once and
call the generated methods directly:

```foundation
import foundation.serializer

@serializer.Serializable()
struct Profile {
    @serializer.Name("first_name")
    FirstName String
    Age i32
    Nick Option<String>

    @serializer.Ignore()
    Secret String = "hidden"
}

const profile = Profile {
    FirstName = "Ada"
    Age = 30
    Nick = .None
}
const encoded = profile.Marshal() else error {
    return .Err(error)
}
const decoded = Profile.Unmarshal(encoded) else error {
    return .Err(error)
}
```

`Marshal` and `ToJSON` borrow the source. `Unmarshal` and `FromJSON` return a new owner. Every failure
is a `Result<_, serializer.Error>` with a stable kind, field path, and optional JSON parser details.

## Policy

`Default()` returns Pascal field names, includes `None` and zero values, and ignores unknown input
fields. Change its fields before passing it to `MarshalWith` or `UnmarshalWith`:

```foundation
var policy = serializer.Default()
policy.Naming = .SnakeCase
policy.IgnoreNone = true
policy.IgnoreZero = true
policy.RejectUnknown = true
```

An explicit `@serializer.Name` is not transformed by the naming policy. `@serializer.Ignore`
excludes the field in both directions.

## Custom codecs

Declare both typed methods on a serializable type:

```foundation
import std.json
import foundation.serializer

@serializer.Serializable()
struct UserID {
    Value String

    @serializer.Encode()
    fn encode(self, policy serializer.Policy) Result<json.Value, serializer.Error> {
        discard policy
        .Ok(.Text(self.Value))
    }

    @serializer.Decode()
    fn decode(
        $value json.Value,
        policy serializer.Policy
    ) Result<UserID, serializer.Error> {
        discard policy
        const encoded = serializer.Text($value, "") else error {
            return .Err(error)
        }
        .Ok(UserID { Value = encoded })
    }
}
```

The compiler checks both signatures and rejects incomplete pairs. A custom type participates in
nested derived codecs without a runtime registration table.
