# Security policy

Security fixes are provided for the latest stable toolchain release. Language 1 source and ABI
compatibility remain subject to the security exception defined in
[docs/compatibility.md](docs/compatibility.md): a fix may reject code that depended on an unsound or
unsafe compiler defect.

Report a vulnerability with GitHub's private vulnerability reporting form under the repository
Security tab. Do not open a public report before a fix or disclosure date is agreed. Include the
affected version, operating system and architecture, a minimal reproducer, expected impact, and any
known workaround.

The maintainer aims to acknowledge a complete report within seven calendar days. Triage assigns an
affected surface, severity, compatibility impact, and disclosure plan. A release that fixes the
report credits the reporter unless anonymity is requested.

Reports about memory safety, ownership checking, generated native code, package integrity, plugin
boundaries, sandbox escape, cryptography, or release artifacts are in scope. Reports that only
change formatting, advisory lint output, or undocumented diagnostic prose are not security issues.
