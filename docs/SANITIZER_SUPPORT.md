# Sanitizer support

LightPHI has presets for AddressSanitizer, UndefinedBehaviorSanitizer and
ThreadSanitizer. Nothing is instrumented by default.

| Preset | Recipe | Enables | Build directory |
|---|---|---|---|
| `dev-asan` | `just build-asan`, `just test-asan` | ASan + UBSan | `build/asan/` |
| `dev-ubsan` | `just build-ubsan`, `just test-ubsan` | UBSan | `build/ubsan/` |
| `dev-tsan` | `just test-tsan` | TSan | `build/tsan/` |

Each inherits the `debug` preset, so tests and examples are built too.

## What they are for here

LightPHI allocates all of its storage once, at source creation, and then copies
into fixed queues and arenas while input flows. The interesting failures are
therefore out-of-bounds indexing of a ring buffer and reading a delivery slot that
has been recycled. ASan is the tool for both, and
`tests/apple_delivery_contract.cpp` and `tests/fake_contract.cpp` are the tests
worth running under it, since they are the ones that drive the queues to their
bounds.

UBSan covers the numeric side: the library converts AppKit values to normalized
ranges and packs capability bits, so signed overflow and bad enum values are real
risks.

TSan is the least useful today. A source is documented as serialized by its owner
and delivers synchronously on the capture thread, so there is no concurrency inside
LightPHI to find. It is present so a consumer can build their own threaded code
against an instrumented LightPHI.

## Not part of the default gate

`just debug-test` does not run them. They are slower, and ASan and TSan cannot be
combined. Run them when changing queue indexing, delivery ownership, or the value
conversions, and before a release.

## Notes

Compile options stay `PRIVATE` and link options are `PUBLIC`, so a consumer linking
an instrumented LightPHI also links the runtime it needs.

On MSVC only ASan is supported; the other two log a warning and are ignored.

A sanitizer build needs a matching runtime for the selected compiler. When a
sanitized binary fails to link, the usual cause is a compiler whose runtime library
is not installed, rather than anything in this project.
