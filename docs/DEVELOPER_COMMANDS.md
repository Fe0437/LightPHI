# Developer commands

`just` with no arguments lists every command. Each recipe is a thin wrapper over a
CMake preset, so everything here can be done with `cmake` directly.

## Build and test

| Command | Does | Output |
|---|---|---|
| `just build` | Release build, no tests or examples | `build/release/` |
| `just debug` | Debug build, no tests run | `build/debug/` |
| `just debug-smoke` | Debug build, then the `smoke`-labeled tests | `build/debug/` |
| `just debug-test` | Debug build, then the whole suite | `build/debug/` |

`just debug-smoke` is the canonical fast gate. Every test is currently labeled
`smoke`, so it and `just debug-test` run the same set; they diverge the moment a
slower test is added.

Without `just`:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

## Choosing a compiler and SDK

The presets are platform-neutral and hard-code no local path. Select a compiler
through `CXX` or a CMake toolchain file. A Clang and Ninja build needs a matching
`clang-scan-deps` for C++ modules.

```sh
CXX="$(brew --prefix llvm)/bin/clang++" just debug-test
```

On macOS the active SDK must also be supplied, or libc++ headers will not find the
platform headers they include:

```sh
cmake --preset debug -DCMAKE_OSX_SYSROOT="$(xcrun --show-sdk-path)"
```

## Formatting and analysis

| Command | Does |
|---|---|
| `just format` | Applies `.clang-format` to every tracked C/C++ file, and `swift-format` when installed |
| `just tidy-report` | Builds and smokes Debug, then writes `clang_tidy_report.md` without changing source |

Static analysis is configured in `.clang-tidy` and treats every finding as an
error. `pre-commit install` runs the formatter automatically on each commit.

The guideline checker enforces the rules in
[the API guidelines](API_GUIDELINES.md) and [the architecture](ARCHITECTURE.md):

```sh
python3 .guidelines-cache/check_guidelines.py
```

## Documentation

`just generate-docs` builds this site into `build/docs/html/`. The Sphinx toolchain
is fetched into `build/docs-venv/` on first use, and the script finds a Python new
enough to host it rather than requiring you to run it with one.

Nothing generated is written into `docs/`: the prose is staged into `build/docs/src/`
and built from there, so `docs/` holds only what a person wrote. Warnings are errors,
so a cross-reference that stops resolving fails the command.

LightPHI is consumed as a submodule, and a parent project's documentation build looks
for the site at `build/docs/html`. Keep that output path.

## Sanitizers

See [sanitizer support](SANITIZER_SUPPORT.md). These are not part of the default
gate.

| Command | Sanitizers |
|---|---|
| `just build-asan` / `just test-asan` | Address and undefined behavior |
| `just build-ubsan` / `just test-ubsan` | Undefined behavior |
| `just test-tsan` | Thread |

## Examples

A debug build produces them in `build/debug/bin/`:

```sh
./build/debug/bin/lightphi_validate_sample   # check a sample against capabilities
./build/debug/bin/lightphi_scripted_input    # drive a receiver with deterministic input
./build/debug/bin/lightphi_selected_source   # use the platform backend
```

`lightphi_selected_source` needs a platform backend and
`lightphi_scripted_input` needs the fake source, so each is built only when its
target exists.

## Cleaning

`just clean` removes every generated build tree. Source files are untouched.
