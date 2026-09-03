# Governance and releases

Foundation is a maintainer-led project. Mirko Brombin is the current maintainer and owns final
decisions on language design, compatibility, releases, and repository access. Design decisions that
change public behavior are recorded in the repository through proposal pull requests rather than
kept in private project notes.

## Contributor and maintainer path

Anyone may submit a focused pull request. Review is based on the language specification, the
compatibility contract, tests, and the repository conventions in
[CONTRIBUTING.md](../CONTRIBUTING.md).

A contributor may become a reviewer after sustained work across implementation and tests. A
reviewer may become a maintainer after demonstrating sound compatibility judgment, reviewing work
from other contributors, and completing the release procedure from a clean checkout. Repository
and release access is granted only after the role is named publicly in this document.

The project currently has one maintainer. This is stated directly instead of implying a larger
team. The path above is how release and review authority will be distributed as contributors earn
it.

## Language decisions

Small fixes that restore specified behavior use an ordinary pull request. A change to syntax,
semantics, ownership, public diagnostics, the standard library contract, Foundation package APIs,
or a stable ABI requires a proposal under `docs/proposals/`.

The proposal stays in the same pull request as its initial implementation or lands before it. It
must describe the user problem, exact rules, compatibility, diagnostics, implementation impact,
tests, and rejected alternatives. Approval records the decision in the merged proposal. Rejection
leaves no normative document in the main branch.

## Releases

The project targets one stable toolchain release per calendar quarter while active development is
producing changes. Compatibility and security fixes may produce an earlier patch release. A release
is cut only from a green `main` commit and uses a `vMAJOR.MINOR.PATCH` tag.

The release workflow rebuilds and tests the compiler on Linux, macOS, and Windows, verifies the
self-hosted fixed point, installs each SDK into a clean staging directory, creates archives and
SHA-256 files, and publishes the release only after all platform jobs succeed. A failed job leaves
the release as a draft.

Release notes identify language changes, compatibility fixes, deprecations, security changes, and
known limitations. They do not call an unimplemented design an available feature. The exact release
commands and checks are in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Continuity

The compiler source, self-hosted bootstrap, runtime, package sources, release workflow, and tests are
all in this repository. A release does not depend on an unpublished generator or private build
service. The MIT or Apache-2.0 license permits continued maintenance and redistribution by a fork if
the original maintainer becomes unavailable.

This does not pretend that repository ownership transfers automatically. It makes the technical and
legal continuation path reproducible without private source or a particular compiler binary. New
maintainers will be listed here with their role before they receive release access.

Compatibility commitments are defined in [compatibility.md](compatibility.md). They remain binding
regardless of who performs a release.
