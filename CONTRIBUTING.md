# Contributing

Open an issue before a change that adds a public API, dependency, backend, or
package. Small fixes may go directly to a pull request.

Contributions are submitted under MPL-2.0. Sign each commit with
`Signed-off-by:` to certify the [Developer Certificate of Origin 1.1](https://developercertificate.org/).
The project does not require a contributor licence agreement.

Use a short gitmoji commit subject. Keep platform types inside their backend.
Add or update the shared conformance tests for contract behavior. Run:

```sh
just debug-test
```

Do not commit build products. A maintainer review is required before merge.
Changes to an exported module API or native layout need an explicit
compatibility review. LightPHI is compiled with its consumer and does not
promise a cross-toolchain C++ ABI.
