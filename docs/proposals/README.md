# Language proposals

A proposal records a change to Foundation syntax, semantics, ownership, public diagnostics,
standard-library contracts, Foundation package APIs, or a stable ABI. Create
`docs/proposals/NNNN-short-name.md` in the pull request that introduces the change. Use the next
unused four-digit number.

Every proposal uses this structure:

```markdown
# NNNN: Title

Status: proposed

## User problem

Describe the concrete work that is difficult or unsafe today.

## Design

State the syntax and behavior precisely. Include short accepted and rejected examples.

## Compatibility

State how existing Language 1 source, packages, native libraries, and plugins are affected.

## Diagnostics

List new or changed diagnostic codes and their trigger conditions.

## Implementation

Identify the stage0, self-hosted, runtime, package, tooling, and documentation surfaces involved.

## Tests

List the conformance cases that prove the change and its compatibility boundary.

## Alternatives

Record the serious alternatives and why this design was selected.
```

The status becomes `accepted` only when the maintainer approves the language decision. An accepted
proposal and its implementation must agree before release. Superseding a proposal requires another
proposal that names it and preserves the commitments in
[compatibility.md](../compatibility.md).
