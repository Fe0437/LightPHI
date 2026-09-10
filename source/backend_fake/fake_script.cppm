/**
 * {file}
 * {brief} What a scripted delivery must satisfy, and the state one leaves behind.
 *
 * These are the rules a real backend obeys, written down so the fake cannot script a delivery no
 * device could produce. They are separate from the source that queues and dispatches deliveries
 * because they answer a different question: this says whether a batch is one a pen could have sent,
 * not when it is handed over.
 *
 * An implementation partition: it belongs to the fake backend and is visible to nothing else.
 */
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

module lightphi.backend.fake:script;

import lightphi.input;

import :errors;

namespace lightphi::input::fake
{
    struct ScriptState
    {
        bool                                           ContactOpen{};
        ContactId                                      Contact{};
        std::uint64_t                                  LastBatchOrdinal{};
        std::uint64_t                                  LastSampleId{};
        std::uint64_t                                  LastCommittedSequence{};
        std::uint64_t                                  LastTimestamp{};
        std::uint32_t                                  PreviousPredictionCount{};
        std::array<SampleId, MaximumBatchSamples>      PreviousPredictionIds{};
        std::array<std::uint64_t, MaximumBatchSamples> PreviousPredictionSequences{};
    };

    [[nodiscard]] bool MatchesPreviousPrediction(const ScriptState &state, const InputSample &sample) noexcept
    {
        const auto ids{std::span{state.PreviousPredictionIds}.first(state.PreviousPredictionCount)};
        const auto sequences{std::span{state.PreviousPredictionSequences}.first(state.PreviousPredictionCount)};
        for (std::size_t index{}; index < ids.size(); ++index)
        {
            if (ids[index] == sample.Corrects && sequences[index] == sample.Sequence)
            {
                return true;
            }
        }
        return false;
    }

    /** {brief} Running totals accumulated while validating the samples of one batch. */
    struct BatchScan
    {
        std::uint64_t SampleId{};
        std::uint64_t CommittedSequence{};
        std::uint64_t Timestamp{};
        std::uint32_t PredictionCount{};
    };

    /** {brief} Check the batch envelope: identity, phase, and sample-count rules. */
    [[nodiscard]] std::optional<FakeSourceError> ValidateEnvelope(const InputBatch              &batch,
                                                                  const InputSourceCapabilities &capabilities) noexcept
    {
        if (batch.Contact.Value == 0U || batch.Phase > LastContactPhase)
        {
            return FakeSourceError::InvalidBatch;
        }
        if (batch.Samples.size() > capabilities.MaximumBatchSamples || batch.Samples.size() > MaximumBatchSamples)
        {
            return FakeSourceError::BatchTooLarge;
        }
        if ((batch.Phase == ContactPhase::Begin || batch.Phase == ContactPhase::Update) && batch.Samples.empty())
        {
            return FakeSourceError::InvalidBatch;
        }
        if (batch.Phase == ContactPhase::Cancel && !batch.Samples.empty())
        {
            return FakeSourceError::InvalidBatch;
        }
        return std::nullopt;
    }

    /** {brief} Check one corrected sample: it names a prior prediction and corrects it only once. */
    [[nodiscard]] std::optional<FakeSourceError> ValidateCorrected(const ScriptState &previous, const InputBatch &batch,
                                                                   std::size_t index, BatchScan &scan) noexcept
    {
        const InputSample &sample{batch.Samples[index]};
        if (!MatchesPreviousPrediction(previous, sample))
        {
            return FakeSourceError::InvalidBatch;
        }
        for (std::size_t earlier{}; earlier < index; ++earlier)
        {
            if (batch.Samples[earlier].Origin == SampleOrigin::Corrected &&
                batch.Samples[earlier].Corrects == sample.Corrects)
            {
                return FakeSourceError::InvalidBatch;
            }
        }
        scan.CommittedSequence = std::max(scan.CommittedSequence, sample.Sequence);
        return std::nullopt;
    }

    /** {brief} Check one predicted sample and record it for the next batch's corrections. */
    [[nodiscard]] std::optional<FakeSourceError>
    ValidatePredicted(const InputSample &sample, BatchScan &scan, std::span<SampleId> predictionIds,
                      std::span<std::uint64_t> predictionSequences) noexcept
    {
        if (sample.Sequence <= scan.CommittedSequence ||
            (scan.PredictionCount > 0U && sample.Sequence <= predictionSequences[scan.PredictionCount - 1U]))
        {
            return FakeSourceError::InvalidBatch;
        }
        predictionIds[scan.PredictionCount]       = sample.Id;
        predictionSequences[scan.PredictionCount] = sample.Sequence;
        ++scan.PredictionCount;
        return std::nullopt;
    }

    /** {brief} Check one measured or coalesced sample and commit its sequence. */
    [[nodiscard]] std::optional<FakeSourceError> ValidateObserved(const InputSample &sample, BatchScan &scan) noexcept
    {
        if (sample.Sequence <= scan.CommittedSequence)
        {
            return FakeSourceError::InvalidBatch;
        }
        scan.CommittedSequence = sample.Sequence;
        return std::nullopt;
    }

    /** {brief} Validate every sample of one batch in order, accumulating the running totals. */
    [[nodiscard]] std::optional<FakeSourceError> ScanSamples(const InputBatch              &batch,
                                                             const InputSourceCapabilities &capabilities,
                                                             const ScriptState &previous, BatchScan &scan,
                                                             std::span<SampleId>      predictionIds,
                                                             std::span<std::uint64_t> predictionSequences) noexcept
    {
        std::uint64_t lastSequenceInBatch{};
        for (std::size_t index{}; index < batch.Samples.size(); ++index)
        {
            const InputSample &sample{batch.Samples[index]};
            if (const auto validity{input::Validate(sample, capabilities)}; !validity)
            {
                return validity.error().Reason == SampleValidationReason::MissingCapability
                           ? FakeSourceError::UndeclaredValue
                           : FakeSourceError::InvalidBatch;
            }
            if (sample.Id.Value <= scan.SampleId || (index > 0U && sample.Sequence < lastSequenceInBatch) ||
                (Supports(capabilities, InputCapability::Timestamp) && sample.TimeNanoseconds < scan.Timestamp))
            {
                return FakeSourceError::InvalidBatch;
            }
            scan.SampleId  = sample.Id.Value;
            scan.Timestamp = std::max(scan.Timestamp, sample.TimeNanoseconds);

            std::optional<FakeSourceError> failure{};
            if (sample.Origin == SampleOrigin::Corrected)
            {
                failure = ValidateCorrected(previous, batch, index, scan);
            }
            else if (sample.Origin == SampleOrigin::Predicted)
            {
                failure = ValidatePredicted(sample, scan, predictionIds, predictionSequences);
            }
            else
            {
                failure = ValidateObserved(sample, scan);
            }
            if (failure)
            {
                return failure;
            }
            lastSequenceInBatch = sample.Sequence;
        }
        return std::nullopt;
    }

    /**
     * {brief} Check one delivery against the rules and report the state it would leave.
     * {param script} What the deliveries before this one established.
     */
    [[nodiscard]] inline std::expected<ScriptState, FakeSourceError>
    NextScriptState(const InputBatch &batch, const InputSourceCapabilities &capabilities, const ScriptState &script)
    {
        if (const auto envelope{ValidateEnvelope(batch, capabilities)})
        {
            return std::unexpected{*envelope};
        }

        ScriptState next{script};
        if (batch.Phase == ContactPhase::Begin)
        {
            if (next.ContactOpen)
            {
                return std::unexpected{FakeSourceError::InvalidBatch};
            }
            next.ContactOpen             = true;
            next.Contact                 = batch.Contact;
            next.LastBatchOrdinal        = batch.BatchOrdinal;
            next.LastSampleId            = 0U;
            next.LastCommittedSequence   = 0U;
            next.LastTimestamp           = 0U;
            next.PreviousPredictionCount = 0U;
        }
        else if (!next.ContactOpen || next.Contact != batch.Contact || batch.BatchOrdinal <= next.LastBatchOrdinal)
        {
            return std::unexpected{FakeSourceError::InvalidBatch};
        }

        std::array<SampleId, MaximumBatchSamples>      predictionIdStorage{};
        std::array<std::uint64_t, MaximumBatchSamples> predictionSequenceStorage{};
        const std::span                                predictionIds{predictionIdStorage};
        const std::span                                predictionSequences{predictionSequenceStorage};
        BatchScan                                      scan{.SampleId          = next.LastSampleId,
                                                            .CommittedSequence = next.LastCommittedSequence,
                                                            .Timestamp         = next.LastTimestamp};
        if (const auto failure{ScanSamples(batch, capabilities, next, scan, predictionIds, predictionSequences)})
        {
            return std::unexpected{*failure};
        }

        next.LastBatchOrdinal        = batch.BatchOrdinal;
        next.LastSampleId            = scan.SampleId;
        next.LastCommittedSequence   = scan.CommittedSequence;
        next.LastTimestamp           = scan.Timestamp;
        next.PreviousPredictionCount = scan.PredictionCount;
        std::copy_n(predictionIds.begin(), scan.PredictionCount, next.PreviousPredictionIds.begin());
        std::copy_n(predictionSequences.begin(), scan.PredictionCount, next.PreviousPredictionSequences.begin());
        if (batch.Phase == ContactPhase::End || batch.Phase == ContactPhase::Cancel)
        {
            next.ContactOpen             = false;
            next.Contact                 = {};
            next.PreviousPredictionCount = 0U;
        }
        return next;
    }
} // namespace lightphi::input::fake
