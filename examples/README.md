# LightPHI examples

- `validate_sample.cpp` checks one observation against a capability set. Needs only
  the input contract.
- `scripted_input.cpp` drives a receiver with deterministic input through the fake
  source, including a busy retry. **This is the pattern to copy into a consumer's own
  tests.** Needs `LightPHI::FakeBackend`.
- `selected_source.cpp` creates the automatically selected source, inspects its
  capabilities, and binds a receiver. Needs a platform backend, and prints that the
  source is unavailable when run without an application host.

Each example is built only when the target it needs exists. Build and run them from
the repository root:

```sh
cmake --preset debug
cmake --build --preset debug
./build/debug/bin/lightphi_validate_sample
./build/debug/bin/lightphi_scripted_input
./build/debug/bin/lightphi_selected_source
```
