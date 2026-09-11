# Architecture

LightPHI is a portable C++23 source of pen and tablet observations. It owns
device capabilities, contact lifecycle, sample identity, correction identity,
and bounded delivery. It does not own drawing coordinates, tools, strokes,
rendering, windows, or application policy.

## Boundaries

The stable source contract is the named module `lightphi.input`. A source
reports only values observed by its device and backend. It does not invent
pressure, prediction, correction, tilt, twist, eraser state, or hover state.

Concrete backends implement `IInputSource`. Their operating-system and vendor
types stay private. Applications depend on `LightPHI::LightPHI` plus one
backend target selected at their composition root.

```text
application composition
        |
        v
operating-system backend  --->  lightphi.input  <---  application receiver
        |
        v
platform input framework
```

The fake backend is the separate `LightPHI::FakeBackend` target and
`lightphi.backend.fake` module. Like a LightRHI backend module, it re-exports
the backend-neutral contract and provides its concrete factory. It owns a fixed
queue and sample arena allocated by `CreateFakeInputSource`. `Enqueue` copies
into that storage. `DispatchNext` is synchronous and performs no allocation. A
busy receiver retains the exact delivery for retry and never causes a wait.
Receiver rejection abandons the active contact. A runtime backend failure
notifies the receiver, cancels the active contact, and stops the source.

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

## API and ABI policy

LightPHI is source-built with the C++23 consumer that selects it. It does not
provide a C ABI, a dynamically loaded plugin ABI, or cross-toolchain binary
compatibility. The public modules, exported signatures, enum widths, and value
layouts are still compatibility-sensitive because separately compiled module
targets meet at those boundaries. The core and backend are shared targets built
in the same CMake graph, matching LightRHI's separation of a backend-neutral
contract from a concrete backend.

API compile tests lock the required concepts and signatures. Native layout
tests record the initial 64-bit contract. An incompatible exported change
requires an explicit version review and coordinated rebuild of consumers.
Exceptions are disabled in LightPHI targets. Allocation and platform objects
never cross the source interface.

Calls concerning one source are serialized by the owner. A receiver borrows a
batch only for `Receive` and must not retain its span or call back into the
source.

## Packages and dependencies

[The package map](docs/PACKAGE_MAP.md) is the complete physical inventory. The
input module has no platform dependency. The fake backend depends only on that
module. Backends planned later depend inward on `lightphi.input` and outward on
exactly one platform input framework. No production backend alias exists until
the first operating-system backend is implemented; the fake is never selected
as a production fallback.

## Verification

The fake-backend conformance suite exercises lifecycle, capabilities, bounded
batches, ordering, prediction replacement, cancellation, backpressure, and
failure. API and native ABI tests compile through named-module imports.
Repository checks keep the licence, attribution, governance, package map, and
module boundary present.

No operating-system backend exists in this revision. The first macOS backend
is separate work. It will provide the common `LightPHI::Backend` target and
`lightphi.backend` module, re-export `lightphi.input`, and keep AppKit types
private. Later platform backends replace that target at configure time without
changing consumer imports.
