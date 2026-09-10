# Architecture

LightPHI is a portable C++23 source of pen and tablet observations. It owns
device capabilities, contact lifecycle, sample identity, correction identity,
and bounded delivery. It does not own drawing coordinates, tools, strokes,
rendering, windows, or application policy.

## Backend composition

The stable source contract is the named module `lightphi.input`. Its primary
interface re-exports capability, sample, and source-delivery partitions from
`source/phi`. A source reports only values observed by its device and backend.
It does not invent pressure, prediction, correction, tilt, twist, eraser state,
or hover state.

Concrete backends implement `IInputSource`. Their operating-system and vendor
types stay private. Applications depend on `LightPHI::LightPHI` plus one
`LightPHI::Backend` target. CMake selects that target from the target operating
system. Applications always import `lightphi.backend`; they do not select or
name its implementation.

```text
application receiver
        ^
        | InputBatch
lightphi.backend
        ^
        | framework-free values
platform adapter
        ^
        | native events
operating-system input framework
```

The fake backend exists so a consumer can test its receiver without a device. The
macOS source needs a main thread with an active `NSApplication`, so it cannot run
in continuous integration at all; the fake is how input-handling code gets tested
there. `examples/scripted_input.cpp` is the pattern to copy. It is not eligible for
automatic production selection, and a consumer that adds LightPHI as a
subdirectory must ask for it with `LIGHTPHI_BUILD_FAKE_BACKEND=ON`.

Its separate `LightPHI::FakeBackend` target
and `lightphi.backend.fake` module own a fixed queue and sample arena allocated
by `CreateFakeInputSource`. `Enqueue` copies into that storage. `DispatchNext`
is synchronous and performs no allocation. A busy receiver retains the exact
delivery for retry and never causes a wait. Receiver rejection abandons the
active contact. A runtime backend failure notifies the receiver, cancels the
active contact, and stops the source.

Tests and examples import only `lightphi.input` or the automatically selected
`lightphi.backend`. Three files deliberately name a concrete component; they are
listed under [verification](#verification).

### macOS and Swift interoperability

The macOS composition selects `LightPHI::Backend` and `lightphi.backend`.
CMake builds three implementation pieces behind that public target:

- `LightPHIMacOSCapture` is a private Swift static library. It owns the AppKit
  event monitor and the retained Swift monitor object.
- `LightPHIAppleInput` is a private, framework-free C++ static library. It owns
  contact state and bounded delivery shared by Apple platforms.
- `LightPHIBackend` is the C++ module adapter. It implements `InputSource`, owns
  both private pieces, and translates internal deliveries to the portable
  receiver contract.

The private pieces talk through two abstract interfaces rather than erased
pointers. `IEventTarget` receives captured events; `AppleDeliveryCore` implements
it. `IDeliveryTarget` receives bounded deliveries; the module adapter implements
it. `AppleEventSink` stays a concrete non-polymorphic type because Swift holds it
by raw pointer and calls its methods directly; the dispatch happens inside C++,
where Swift cannot see it.

Swift imports `apple_event_sink.h` through the private
`LightPHIAppleInterop` Clang module map. The sink accepts only integer, floating
point, Boolean, and framework-free C++ values. CMake enables Swift C++
interoperability and asks Swift to generate `LightPHIMacOSCapture-Swift.h`.
`macos_backend.cpp` includes that generated header to create, start, stop, and
release the Swift monitor. No AppKit or Swift ownership type crosses
`lightphi.backend` or `lightphi.input`.

```text
NSEvent
   |
   v
MacOSInputMonitor.swift
   |
   | AppleEventSink: plain captured values
   v
AppleDeliveryCore
   |
   | Delivery: identity, sequence, phase, bounded slot
   v
InputSourceImplementation
   |
   | InputBatch borrowed for this call
   v
IInputReceiver::Receive
```

`Start` must run on the main thread of a process with an active
`NSApplication`. The Swift adapter installs a local monitor, so it observes
tablet events delivered to that application rather than system-wide input. It
tracks eraser state from tablet proximity events and accepts tablet-point mouse
down, drag, up, and cancellation events. Application deactivation also cancels
the active contact.

Before crossing into C++, Swift converts the event timestamp to nanoseconds,
pressure to the AppKit normalized value, tilt to radians in `[-pi/2, pi/2]`,
and rotation from degrees to radians. The shared C++ core clamps pressure and
tilt, assigns identities and sequence numbers, and retains deliveries in fixed
storage. `DeliveryResult::Busy` leaves the exact queue slot for retry.
`Rejected` abandons the contact. Queue exhaustion reports
`InputSourceError::Overflow` and stops capture.

The shared Apple core contains no AppKit or UIKit dependency. A future iOS
backend can reuse it while providing its own Swift UIKit adapter. UIKit capture
will need an application-owned view or responder attachment contract, so the
macOS event monitor itself is not shared.

## Contact and correction rules

A source has at most one active contact. `Begin` opens it, `Update` extends it,
and `End` or `Cancel` closes it. Batch ordinals increase within the contact.
`Begin` and `Update` are nonempty. `Cancel` is empty. `End` may carry a last
sample.

Sample identities are nonzero and increase in delivery order. Committed
sequences increase. Predictions may name later sequences. A correction has a
new sample identity, repeats the prediction's sequence, and names a prediction
from the preceding batch. This keeps correction storage bounded and makes
replacement explicit.

## Known limitations

### A capability set is observed, not fixed

`Validate` checks one sample against one `InputSourceCapabilities` value, which
reads as though the set were fixed for the life of a source. The macOS source does
not behave that way.

Its set starts as `Timestamp` alone. A tablet proximity event replaces it with the
mask AppKit reports for the device. Delivering a nonzero pressure, tilt, or twist
value then adds that bit. So a consumer that reads `Capabilities()` once after
`Start` and validates later samples against that copy will reject valid samples
with `MissingCapability`.

The fake source takes its capabilities as a fixed descriptor and validates every
scripted batch against it. The two sources therefore disagree about whether a
capability set may grow during a session.

This is not yet decided. Either a source declares at creation the union of
everything it may ever deliver, or a capability set is explicitly a live
observation and validation uses the set current at delivery time. Until it is
decided, read `Capabilities()` immediately before validating and do not cache it
across deliveries.

## API and build-boundary policy

LightPHI is source-built with the C++23 consumer that selects it. It does not
provide a C ABI, a dynamically loaded plugin ABI, or cross-toolchain binary
compatibility. The core and backend are shared targets built in the same CMake
graph, matching LightRHI's separation of a backend-neutral contract from a
concrete backend. Consumers rebuild when an exported module contract changes.

API compile tests lock the required concepts and signatures through the core
and automatically selected backend targets. Hard-coded standard-library and
class sizes are intentionally absent. Exceptions are disabled in LightPHI
targets. Allocation and platform objects never cross the source interface.

Calls concerning one source are serialized by the owner. A receiver borrows a
batch only for `Receive` and must not retain its span or call back into the
source.

## Packages and dependencies

[The package map](PACKAGE_MAP.md) is the complete physical inventory. The
input module has no platform dependency. The fake backend depends only on that
module. The private Apple delivery target has no platform framework dependency.
The macOS backend depends inward on both and uses AppKit only in its Swift
capture adapter. CMake selects it automatically on macOS; the fake is never
selected as a production fallback.

An iOS backend is not present. UIKit requires capture to attach to an
application-owned view or responder, unlike the self-contained AppKit event
monitor. Its future attachment contract must remain outside `lightphi.input`.

## Verification

Tests fall into two groups, and the difference is deliberate.

Consumer tests see only what an application sees. The input contract test imports
only `lightphi.input`. The backend contract test imports only `lightphi.backend`
and links the backend CMake selected. Neither names a concrete backend. Examples
are consumers too and use the same selected contract.

Component tests verify one implementation piece directly, so they must name it.
`tests/fake_contract.cpp` imports `lightphi.backend.fake`, and
`tests/apple_delivery_contract.cpp` links the private `LightPHIAppleInput` target
and includes its header.

`examples/scripted_input.cpp` names the fake source for the same reason: its whole
subject is how a consumer scripts deterministic input, which it cannot show without
importing it.

Those three are the complete list. No application, and no other test or example, may
select a concrete backend.

[The test strategy](TEST_STRATEGY.md) explains what each test owns.
