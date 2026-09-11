# LightPHI

LightPHI is a small, portable C++23 pen and tablet input interface. Its
`lightphi.input` module reports observed device capabilities and delivers
identified samples in bounded batches. Prediction corrections explicitly name
the sample they replace.

The current source tree provides the contract and a deterministic fake backend.
It does not yet contain an operating-system backend.

## Build and test

LightPHI has no third-party dependencies. A CMake 3.28 or newer build with a
C++23 compiler and named-module support is sufficient.

Select a compiler with a matching `clang-scan-deps` through `CXX` or a CMake
toolchain file. On macOS, also provide the active SDK through
`CMAKE_OSX_SYSROOT`. The presets do not hard-code either machine-specific path.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Consumers that use this repository with `add_subdirectory` link
`LightPHI::LightPHI` plus one concrete backend, following the same split as
LightRHI. Tests link `LightPHI::FakeBackend` and import
`lightphi.backend.fake`, which re-exports `lightphi.input`. A production backend
will be added separately and the fake will not be selected as its fallback.
The fake backend and tests default on for a standalone checkout and off when
LightPHI is added as a subdirectory.

Read [the architecture](ARCHITECTURE.md) for delivery and ABI rules and
[the contribution guide](CONTRIBUTING.md) before proposing a change.

The project is licensed under MPL-2.0. Keep copyright and licence notices with
redistributed source. The official repository is
https://github.com/Fe0437/LightPHI.
