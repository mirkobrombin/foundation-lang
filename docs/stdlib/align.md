# `std.align`

`std.align` rounds unsigned integers to power-of-two boundaries while preserving their machine
width. The package exports paired upward and downward functions for `u8`, `u16`, `u32`, `u64`, and
`usize`:

```foundation
import std.align

const pageStart = align.DownUsize(address, 4096)
const nextPage = align.UpUsize(address, 4096)
```

The complete surface is:

```foundation
fn UpU8(value u8, alignment u8) u8
fn DownU8(value u8, alignment u8) u8
fn UpU16(value u16, alignment u16) u16
fn DownU16(value u16, alignment u16) u16
fn UpU32(value u32, alignment u32) u32
fn DownU32(value u32, alignment u32) u32
fn UpU64(value u64, alignment u64) u64
fn DownU64(value u64, alignment u64) u64
fn UpUsize(value usize, alignment usize) usize
fn DownUsize(value usize, alignment usize) usize
```

An alignment of one is valid. Zero and non-power-of-two alignments violate the API precondition and
panic with a Foundation trace. Upward alignment also uses checked arithmetic, so a result outside
the selected width panics instead of wrapping. Downward alignment cannot overflow.

The package uses division and remainder rather than exposing a compiler intrinsic. This keeps
alignment in the standard library and gives every backend the same checked behavior.
