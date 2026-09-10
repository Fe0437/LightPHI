# LightPHI development guide

LightPHI is a standalone C++23 modules library: a portable pen and tablet input
interface with one core contract and one automatically selected platform backend.
Read `docs/API_GUIDELINES.md` before changing an exported contract and
`docs/ARCHITECTURE.md` before moving responsibilities or adding a dependency.
`docs/PACKAGE_MAP.md` is the physical inventory of where each file belongs.

## Hard rules

- Change the minimum needed for the requested behavior. Preserve unrelated work.
- Use a short gitmoji commit subject, and sign off with `Signed-off-by:` as
  `CONTRIBUTING.md` requires.
- Keep reusable agent workflows in the `skills` submodule. Keep project paths, policy,
  commands and changing instructions in this repository. `.agents/skills` is the tracked
  discovery link to `../skills`.
- Write documentation and comments in plain, short sentences with ordinary words. Do not
  invent terminology. Less text that is clear beats more text that is dense.
- Apply YAGNI: no field, method, abstraction, dependency, or option without a concrete
  story.
- The only Python is the documentation build under `tools/docs/`. It is governed by
  `ruff.toml`. `tools/docs/build_docs.py` selects a newer interpreter for the toolchain,
  so that file must keep running under the Python an operating system supplies.
- Platform types never leave their backend. `source/phi/` is framework-free and must not
  include AppKit, Windows, or any other operating-system header.
- A backend implements `IInputSource` and reports only capabilities observed from the
  device. A production backend never falls back to the fake backend.
- The public surface is C++ named modules only. Do not add a C ABI, an Objective-C
  implementation layer, or a runtime backend switch.
- Do not commit build products, generated reports, or `.guidelines-cache/`.

## Naming and style

- Module names and namespaces use lowercase dotted/snake-case vocabulary:
  `lightphi.input` and `lightphi::input`.
- Exported types, enum values, functions, methods, and aggregate fields use `PascalCase`.
- Abstract interfaces use an `I` prefix. Locals and parameters use `lowerCamelCase`.
- Private members and helpers use `_camelCase`; source directories and file names use
  `snake_case`.
- Use brace initialization, designated initializers for descriptors and POD schemas, RAII,
  and `[[nodiscard]]` where a result must be consumed. Do not use raw `new`/`delete`.
- The library is compiled with `-fno-exceptions -fno-rtti`. Report failure through
  `std::expected` and `InputSourceError`, never by throwing.
- Format with the repository `.clang-format`; never hand-format around it.
- Start every C/C++ source and header with a `{file}`/`{brief}` block that states its
  responsibility and its exclusions. Document every exported type, enum value, aggregate
  field, function, and public method at its declaration.

## Verification

- Presets are platform-neutral. Select the compiler through `CXX` or a CMake toolchain
  file; never add an OS-named build directory or a hard-coded local compiler path.
  Clang/Ninja builds require a matching `clang-scan-deps`.
- `just debug-smoke` is the canonical fast gate. Do not weaken, skip, or detach smoke
  tests from that recipe. Run `just debug-test` before handoff.
- Add or update the shared contract tests for any change to delivery behavior.
- Use the repository-local skills for clang-tidy and guideline checks. Project-specific
  clang-tidy auto-fix exclusions belong in `.clang-tidy-autofix.json` and must be
  explained here.
- `misc-include-cleaner` is disabled because clang-tidy 21 treats declarations supplied by
  C++ `import` statements as missing header includes. Re-enable it only after the checker
  understands named-module providers.
- Sanitizer presets (`just test-asan`, `just test-ubsan`, `just test-tsan`) are available
  and not part of the default gate.
- `clang-analyzer-optin.core.EnumCastOutOfRange` is disabled because `InputCapability` is a bitmask
  enum whose `operator|`/`operator&` cast a combined integer back to the enum by design. The checker
  has no model for flag enums.
