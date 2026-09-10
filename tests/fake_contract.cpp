/**
 * {file}
 * {brief} Proves the deterministic fake backend enforces the scripted delivery contract.
 *
 * Operating-system capture stays in the backend contract test. This file exercises only
 * descriptor validation, batch validation, the fixed queue, and explicit dispatch.
 */
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

import lightphi.backend.fake;

using namespace lightphi::input;
using namespace lightphi::input::fake;

namespace
{
    /** {brief} Capabilities wide enough to script prediction and correction. */
    constexpr InputSourceCapabilities FullCapabilities{.Available =
                                                           InputCapability::Pressure | InputCapability::Timestamp |
                                                           InputCapability::Prediction | InputCapability::Correction,
                                                       .MaximumBatchSamples = 8U};

    /** {brief} Records every delivery so a test can assert what the source produced. */
    class RecordingReceiver final : public IInputReceiver
    {
      public:
        DeliveryResult Decision{DeliveryResult::Accepted};
        std::uint32_t  ReceivedBatches{};
        std::uint32_t  ReceivedSamples{};
        bool           Failed{};

        [[nodiscard]] DeliveryResult Receive(const InputBatch &batch) noexcept override
        {
            if (Decision == DeliveryResult::Accepted)
            {
                ++ReceivedBatches;
                ReceivedSamples += static_cast<std::uint32_t>(batch.Samples.size());
                LastSamples.assign(batch.Samples.begin(), batch.Samples.end());
            }
            return Decision;
        }

        void SourceFailed(InputSourceError /*error*/) noexcept override
        {
            Failed = true;
        }

        std::vector<InputSample> LastSamples{};
    };

    [[nodiscard]] InputSample Measured(std::uint64_t id, std::uint64_t sequence, std::uint64_t time) noexcept
    {
        return InputSample{.Id              = {.Value = id},
                           .Sequence        = sequence,
                           .TimeNanoseconds = time,
                           .Pressure        = 0.5,
                           .Origin          = SampleOrigin::Measured};
    }

    [[nodiscard]] InputSample Predicted(std::uint64_t id, std::uint64_t sequence, std::uint64_t time) noexcept
    {
        InputSample sample{Measured(id, sequence, time)};
        sample.Origin = SampleOrigin::Predicted;
        return sample;
    }

    [[nodiscard]] InputSample Corrected(std::uint64_t id, std::uint64_t sequence, std::uint64_t time,
                                        std::uint64_t corrects) noexcept
    {
        InputSample sample{Measured(id, sequence, time)};
        sample.Origin   = SampleOrigin::Corrected;
        sample.Corrects = {.Value = corrects};
        return sample;
    }

    [[nodiscard]] InputBatch Batch(std::uint64_t contact, std::uint64_t ordinal, ContactPhase phase,
                                   std::span<const InputSample> samples) noexcept
    {
        return InputBatch{.Contact = {.Value = contact}, .BatchOrdinal = ordinal, .Phase = phase, .Samples = samples};
    }

    /** {brief} Descriptor bounds and capability masks the source must reject. */
    [[nodiscard]] bool RejectsInvalidDescriptors() noexcept
    {
        const FakeInputSourceDescriptor zeroBatch{.Capabilities         = {.MaximumBatchSamples = 0U},
                                                  .MaximumQueuedBatches = 4U};
        const FakeInputSourceDescriptor zeroQueue{.Capabilities = FullCapabilities, .MaximumQueuedBatches = 0U};
        const FakeInputSourceDescriptor hugeQueue{.Capabilities         = FullCapabilities,
                                                  .MaximumQueuedBatches = MaximumQueuedBatches + 1U};
        // Correction without prediction is contradictory and must be refused.
        const FakeInputSourceDescriptor correctionOnly{
            .Capabilities         = {.Available = InputCapability::Correction, .MaximumBatchSamples = 4U},
            .MaximumQueuedBatches = 4U};
        // An invented capability bit outside AllInputCapabilities must be refused.
        const FakeInputSourceDescriptor inventedBit{
            .Capabilities         = {.Available = static_cast<InputCapability>(0x8000U), .MaximumBatchSamples = 4U},
            .MaximumQueuedBatches = 4U};

        const std::array<FakeInputSourceDescriptor, 5> invalid{zeroBatch, zeroQueue, hugeQueue, correctionOnly,
                                                               inventedBit};
        return std::ranges::all_of(invalid,
                                   [](const FakeInputSourceDescriptor &descriptor)
                                   {
                                       const auto source{CreateFakeInputSource(descriptor)};
                                       return !source && source.error() == FakeSourceError::InvalidDescriptor;
                                   });
    }

    /** {brief} Contact, ordinal, phase and emptiness rules enforced by batch validation. */
    [[nodiscard]] bool RejectsInvalidBatches() noexcept
    {
        const auto source{CreateFakeInputSource({.Capabilities = FullCapabilities, .MaximumQueuedBatches = 4U})};
        if (!source)
        {
            return false;
        }
        const InputSample                one{Measured(1U, 1U, 10U)};
        const std::array<InputSample, 1> single{one};
        const std::array<InputSample, 9> tooMany{one, one, one, one, one, one, one, one, one};

        // Contact zero is never a valid identity.
        if ((*source)->Enqueue(Batch(0U, 1U, ContactPhase::Begin, single)))
        {
            return false;
        }
        // A batch larger than the advertised bound is refused as BatchTooLarge.
        const auto oversized{(*source)->Enqueue(Batch(1U, 1U, ContactPhase::Begin, tooMany))};
        if (oversized || oversized.error() != FakeSourceError::BatchTooLarge)
        {
            return false;
        }
        // Begin and Update must carry at least one sample.
        if ((*source)->Enqueue(Batch(1U, 1U, ContactPhase::Begin, {})))
        {
            return false;
        }
        // Cancel must carry none.
        if ((*source)->Enqueue(Batch(1U, 1U, ContactPhase::Cancel, single)))
        {
            return false;
        }
        // Update before Begin has no open contact.
        if ((*source)->Enqueue(Batch(1U, 1U, ContactPhase::Update, single)))
        {
            return false;
        }
        // A sample using an undeclared capability is reported distinctly.
        InputSample tilted{one};
        tilted.TiltXRadians = 0.5;
        const std::array<InputSample, 1> tiltedBatch{tilted};
        const auto undeclared{(*source)->Enqueue(Batch(1U, 1U, ContactPhase::Begin, tiltedBatch))};
        if (undeclared || undeclared.error() != FakeSourceError::UndeclaredValue)
        {
            return false;
        }

        // A well-formed Begin is accepted, after which a second Begin and a stale ordinal are not.
        if (!(*source)->Enqueue(Batch(1U, 1U, ContactPhase::Begin, single)))
        {
            return false;
        }
        if ((*source)->Enqueue(Batch(1U, 2U, ContactPhase::Begin, single)))
        {
            return false;
        }
        const std::array<InputSample, 1> later{Measured(2U, 2U, 20U)};
        if ((*source)->Enqueue(Batch(1U, 1U, ContactPhase::Update, later)))
        {
            return false;
        }
        // A different contact cannot extend the open one.
        if ((*source)->Enqueue(Batch(2U, 2U, ContactPhase::Update, later)))
        {
            return false;
        }
        return true;
    }

    /** {brief} Sample ordering rules: identity, sequence, and timestamp must advance. */
    [[nodiscard]] bool RejectsMisorderedSamples() noexcept
    {
        const auto source{CreateFakeInputSource({.Capabilities = FullCapabilities, .MaximumQueuedBatches = 4U})};
        if (!source)
        {
            return false;
        }
        // Sample identity must strictly increase across the contact.
        const std::array<InputSample, 2> repeatedId{Measured(1U, 1U, 10U), Measured(1U, 2U, 20U)};
        if ((*source)->Enqueue(Batch(1U, 1U, ContactPhase::Begin, repeatedId)))
        {
            return false;
        }
        // Sequence must not move backwards within one batch.
        const std::array<InputSample, 2> backwardSequence{Measured(1U, 5U, 10U), Measured(2U, 4U, 20U)};
        if ((*source)->Enqueue(Batch(1U, 1U, ContactPhase::Begin, backwardSequence)))
        {
            return false;
        }
        // A declared timestamp must not move backwards.
        const std::array<InputSample, 2> backwardTime{Measured(1U, 1U, 30U), Measured(2U, 2U, 20U)};
        return !(*source)->Enqueue(Batch(1U, 1U, ContactPhase::Begin, backwardTime)).has_value();
    }

    /** {brief} Prediction and correction rules across two batches of one contact. */
    [[nodiscard]] bool EnforcesPredictionRules() noexcept
    {
        const auto source{CreateFakeInputSource({.Capabilities = FullCapabilities, .MaximumQueuedBatches = 8U})};
        if (!source)
        {
            return false;
        }
        // Begin commits sequence 1 and predicts sequences 2 and 3.
        const std::array<InputSample, 3> opening{Measured(1U, 1U, 10U), Predicted(2U, 2U, 20U), Predicted(3U, 3U, 30U)};
        if (!(*source)->Enqueue(Batch(1U, 1U, ContactPhase::Begin, opening)))
        {
            return false;
        }
        // A correction naming a sample that was never predicted is refused.
        const std::array<InputSample, 1> unknownCorrection{Corrected(4U, 2U, 40U, 99U)};
        if ((*source)->Enqueue(Batch(1U, 2U, ContactPhase::Update, unknownCorrection)))
        {
            return false;
        }
        // Correcting the same prediction twice in one batch is refused.
        const std::array<InputSample, 2> doubleCorrection{Corrected(4U, 2U, 40U, 2U), Corrected(5U, 2U, 41U, 2U)};
        if ((*source)->Enqueue(Batch(1U, 2U, ContactPhase::Update, doubleCorrection)))
        {
            return false;
        }
        // A correction of an actual prediction is accepted.
        const std::array<InputSample, 1> goodCorrection{Corrected(4U, 2U, 40U, 2U)};
        if (!(*source)->Enqueue(Batch(1U, 2U, ContactPhase::Update, goodCorrection)))
        {
            return false;
        }
        // Predictions from the first batch are no longer correctable after the second.
        const std::array<InputSample, 1> staleCorrection{Corrected(5U, 3U, 50U, 3U)};
        return !(*source)->Enqueue(Batch(1U, 3U, ContactPhase::Update, staleCorrection)).has_value();
    }

    /** {brief} The fixed queue bound, dispatch ordering, retry, rejection and injected failure. */
    [[nodiscard]] bool DeliversThroughTheFixedQueue() noexcept
    {
        const auto source{CreateFakeInputSource({.Capabilities = FullCapabilities, .MaximumQueuedBatches = 2U})};
        if (!source)
        {
            return false;
        }
        RecordingReceiver receiver{};

        // Dispatch before Start reports NotStarted.
        const auto early{(*source)->DispatchNext()};
        if (early || early.error() != DispatchError::NotStarted)
        {
            return false;
        }
        if (!(*source)->Start(receiver))
        {
            return false;
        }
        // Starting twice is refused.
        if ((*source)->Start(receiver))
        {
            return false;
        }
        const auto empty{(*source)->DispatchNext()};
        if (empty || empty.error() != DispatchError::QueueEmpty)
        {
            return false;
        }

        const std::array<InputSample, 1> first{Measured(1U, 1U, 10U)};
        const std::array<InputSample, 2> second{Measured(2U, 2U, 20U), Measured(3U, 3U, 30U)};
        if (!(*source)->Enqueue(Batch(1U, 1U, ContactPhase::Begin, first)) ||
            !(*source)->Enqueue(Batch(1U, 2U, ContactPhase::Update, second)))
        {
            return false;
        }
        if ((*source)->QueuedBatchCount() != 2U)
        {
            return false;
        }
        // The queue holds exactly two slots.
        const std::array<InputSample, 1> third{Measured(4U, 4U, 40U)};
        const auto                       full{(*source)->Enqueue(Batch(1U, 3U, ContactPhase::Update, third))};
        if (full || full.error() != FakeSourceError::QueueFull)
        {
            return false;
        }

        // A busy receiver keeps the exact batch queued.
        receiver.Decision = DeliveryResult::Busy;
        const auto busy{(*source)->DispatchNext()};
        if (busy || busy.error() != DispatchError::ReceiverBusy || (*source)->QueuedBatchCount() != 2U)
        {
            return false;
        }

        // Accepted deliveries arrive oldest first and carry the stored samples.
        receiver.Decision = DeliveryResult::Accepted;
        if (!(*source)->DispatchNext() || receiver.ReceivedBatches != 1U || receiver.ReceivedSamples != 1U ||
            receiver.LastSamples.size() != 1U || receiver.LastSamples[0] != first[0])
        {
            return false;
        }
        if (!(*source)->DispatchNext() || receiver.ReceivedBatches != 2U || receiver.ReceivedSamples != 3U ||
            receiver.LastSamples.size() != 2U || receiver.LastSamples[1] != second[1])
        {
            return false;
        }
        if ((*source)->QueuedBatchCount() != 0U)
        {
            return false;
        }

        // A rejected delivery abandons the queue and the active contact.
        if (!(*source)->Enqueue(Batch(1U, 3U, ContactPhase::Update, third)))
        {
            return false;
        }
        receiver.Decision = DeliveryResult::Rejected;
        const auto rejected{(*source)->DispatchNext()};
        if (rejected || rejected.error() != DispatchError::ReceiverRejected || (*source)->QueuedBatchCount() != 0U)
        {
            return false;
        }

        // An injected failure stops the source and notifies the receiver once.
        receiver.Decision = DeliveryResult::Accepted;
        (*source)->FailNext();
        const auto failed{(*source)->DispatchNext()};
        if (failed || failed.error() != DispatchError::BackendFailure || !receiver.Failed)
        {
            return false;
        }
        // After a failure the source is stopped, so dispatch reports NotStarted again.
        const auto afterFailure{(*source)->DispatchNext()};
        return !afterFailure && afterFailure.error() == DispatchError::NotStarted;
    }
} // namespace

int main()
{
    return RejectsInvalidDescriptors() && RejectsInvalidBatches() && RejectsMisorderedSamples() &&
                   EnforcesPredictionRules() && DeliversThroughTheFixedQueue()
               ? 0
               : 1;
}
