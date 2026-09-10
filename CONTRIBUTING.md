# Contributing

Open an issue before a change that adds a public API, dependency, backend, or
package. Small fixes may go directly to a pull request.

Contributions are submitted under MPL-2.0. Sign each commit with
`Signed-off-by:` to certify the [Developer Certificate of Origin 1.1](https://developercertificate.org/).
The project does not require a contributor licence agreement.

Use a short gitmoji commit subject. Keep platform types inside their backend.
Add or update the shared contract tests for delivery behavior. Before proposing a
change, format and run the suite:

```sh
just format
just debug-test
```

`just format` applies `.clang-format`. Install the pre-commit hooks with
`pre-commit install` to have it run automatically. Static analysis is configured in
`.clang-tidy`; `just tidy-report` writes a whole-project report without changing
source. The rules a change must satisfy are in
[`docs/API_GUIDELINES.md`](docs/API_GUIDELINES.md) and
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

Do not commit build products. A maintainer review is required before merge.
Changes to an exported module API need an explicit compatibility review.
LightPHI is compiled with its consumer and does not promise binary compatibility
between builds or toolchains.
