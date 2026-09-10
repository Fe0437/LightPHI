# Glossary

These are the stable meanings of the terms LightPHI uses. A guide may explain how
terms relate. It never gives a term a different meaning.

## Source and receiver

**Source** — an object that produces pen observations, declared by `IInputSource`.
A source has three operations: report its capabilities, start with a receiver, and
stop. Everything else about it is backend-specific.

**Receiver** — the consumer's object that consumes observations, declared by
`IInputReceiver`. A source calls it synchronously and never from another thread.

**Backend** — one concrete implementation of a source. CMake selects one for the
target platform. There is no backend option in application code and no runtime
switch. The current backends are the macOS source and the fake source.

**Delivery** — one synchronous call from a source into a receiver, carrying one
batch. The receiver answers with a `DeliveryResult`:

- **Accepted** — the receiver consumed the batch. The source moves on.
- **Busy** — the receiver cannot take it now. The source keeps the *exact* batch
  and offers it again later. It never waits and never drops it.
- **Rejected** — the receiver abandons the contact. The source discards everything
  queued for it.

## Capabilities

**Capability** — one optional feature a source may report: pressure, coalescing,
prediction, correction, timestamp, tilt, twist, eraser, or hover. Capabilities are
bits in `InputCapability` and combine into a mask.

**Capability set** — `InputSourceCapabilities`: the mask a source reports plus the
largest number of samples it puts in one batch. A source must not populate a
sample field whose capability it does not report. See the limitation in
[the architecture](ARCHITECTURE.md#known-limitations): the macOS set is observed
during a session rather than fixed at its start.

## Contact

**Contact** — one uninterrupted interaction, from the pen touching down to lifting
off or being cancelled. A source has at most one active contact at a time.

**Contact identity** — `ContactId`, the source-local value naming a contact. Zero
means no contact. A new contact always gets a value not used before by that source.

**Contact phase** — where a batch sits in the contact's life:

- **Begin** — opens the contact. Carries at least one sample.
- **Update** — extends it. Carries at least one sample.
- **End** — closes it normally. May carry one last sample.
- **Cancel** — abandons it. Carries no samples.

## Batch and sample

**Batch** — `InputBatch`: one delivery's worth of observations for one contact,
with its phase and ordinal. Its samples are borrowed through a `std::span` that is
valid only for the duration of the `Receive` call. A receiver that needs the data
afterwards must copy it.

**Batch ordinal** — a value that strictly increases across the batches of one
contact. It lets a receiver detect that it is seeing batches in order.

**Sample** — `InputSample`: one observation. Position, and whichever of time,
pressure, tilt, twist, eraser and hover the source reports.

**Sample identity** — `SampleId`. Nonzero, and increasing in delivery order within
a contact. It is what a correction names.

**Sequence** — a sample's logical position within the contact. Distinct from
identity: a correction has a *new* identity but repeats the sequence of the
prediction it replaces.

## Sample origin

**Measured** — a direct observation from the device.

**Coalesced** — an earlier observation the platform batched up rather than
delivering on its own. Requires the coalescing capability.

**Predicted** — a provisional estimate ahead of the pen, offered so a consumer can
draw with less lag. Requires the prediction capability.

**Corrected** — a real observation replacing one earlier prediction. It names that
prediction through its `Corrects` field. Requires the correction capability.

**Prediction** and **correction** are a pair. A correction may only name a
prediction from the immediately preceding batch, which is what keeps the storage a
source needs for them bounded.

## Implementation terms

These name internals. They do not appear in the public API.

**Slot** — one position in a source's fixed delivery queue. A batch keeps its slot
across a busy retry, which is how the retry can offer the identical batch.

**Sample arena** — the fixed sample storage a source allocates once at creation.
Enqueueing copies into it; delivering borrows from it. Nothing allocates while
input is flowing.

**Delivery core** — `AppleDeliveryCore`, the framework-free state machine shared by
Apple backends. It owns contact identity, ordinals, sequences, and the bounded
queue. It has no AppKit dependency, so it is tested directly.
