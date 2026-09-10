# Test strategy

Every test runs from `just debug-test`. All of them are labeled `smoke`, because
the whole suite finishes in about a second; there is no slower tier yet.

## What each test owns

| Test | Sees | Owns |
|---|---|---|
| `tests/api_contract.cpp` | `lightphi.input` only | The exported contract: types are trivially copyable, sizes are stable, `Validate` accepts and rejects the right samples |
| `tests/backend_contract.cpp` | `lightphi.backend` only | That the selected backend satisfies the portable API and reports a sane capability set |
| `tests/apple_delivery_contract.cpp` | `LightPHIAppleInput` directly | The contact state machine: lifecycle, identities, busy retry, rejection, overflow, clamping |
| `tests/fake_contract.cpp` | `lightphi.backend.fake` | The scripted source: descriptor bounds, batch validation, the fixed queue, explicit dispatch |

The first two are **consumer tests**: they see exactly what an application sees and
never name a concrete backend. The last two are **component tests**: they verify one
implementation piece, so they name it. Those are the only two exceptions to the rule
in [the architecture](ARCHITECTURE.md#verification).

## Why the delivery core is tested directly

The macOS source needs a main thread with an active `NSApplication`. A command-line
test process has neither, so `Start` returns `Unavailable` and the whole delivery
path would go unexercised — the ring buffer, contact lifecycle, overflow, busy retry
and rejection among it.

`AppleDeliveryCore` exists precisely so that logic has no framework dependency. A
test implements `IDeliveryTarget`, feeds it `CapturedEvent` values, and asserts on
the deliveries it produces. No window server, no device, no event loop. That is the
point of the core/adapter split, and the test is what makes the split pay.

What remains untested by construction is the Swift adapter: whether an `NSEvent` is
correctly turned into a `CapturedEvent`. That needs a real application host.

## Tests have to be able to fail

A test that passes proves nothing until you have seen it fail for the right reason.
Two habits apply here.

**Write the test against the behavior that already exists.** When
`tests/fake_contract.cpp` was written to cover a refactor, it was first run against
the *pre-refactor* implementation recovered from git. Passing there is what shows it
encodes the old behavior rather than the new code's behavior.

**Mutate the implementation and confirm the test fails.** Break one rule at a time —
drop a clamp, treat `Busy` as `Accepted`, stop resetting on rejection — rebuild, and
check the test goes red. A mutation that survives is a gap.

This found a real gap: deleting the `if (!_started) return;` guard in
`AppleDeliveryCore::Submit` did not fail anything, because `_drain` checks `_started`
too, so no delivery could escape either way. The guard does matter — without it a
device report before `Start` is absorbed into the capability set, which a consumer
can read. The test now asserts capabilities are unchanged outside a session.

When mutating, check the mutation actually applied. A patch that silently fails to
match looks exactly like a test that caught nothing.

## Adding a test

Put it in `tests/`, add it to `tests/CMakeLists.txt` guarded by the target it needs,
label it `smoke`, and give it a `TIMEOUT`. Tests return `0` or `1` from `main` and
use no framework. Prefer one named predicate per behavior so a failure names itself.
