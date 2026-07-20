# Foundation API Reference

## Package `docs.fixture`

### Types

#### `User`

Stores one user record.

```foundation
struct User
```

Implements: `Named`.

Fields:

- `Name String`

  The visible name.

##### Methods

###### `Display`

Prints the visible name.

```foundation
@Operation(...) fn Display(self) void
```

### Enums

#### `Status`

Describes the current user state.

```foundation
enum Status
```

Variants:

- `Ready`

  The user is ready.

### Contracts

#### `Named`

Supplies a visible name.

```foundation
contract Named
```

Methods:

##### `Display`

Prints the visible name.

```foundation
fn Display(self) void
```

### Attributes

#### `@Operation`

Marks a documented operation.

```foundation
attribute Operation(label String)
```

Targets: `function`, `method`. Repeatable.

### Functions

#### `NewUser`

Creates a user record.

```foundation
@Operation(...) fn NewUser($name String) User
```

Parameters:

- `name`

  The visible name.

  Used by **display** output.
