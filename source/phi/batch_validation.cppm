/**
 * {file}
 * {brief} Checks one delivery against the rules every source's delivery stream obeys.
 *
 * A sample can be valid on its own and still be impossible where it sits: a second Begin, an ordinal
 * that goes backwards, a correction naming a prediction nobody sent. Those rules span deliveries, so
 * the check carries what earlier deliveries established. One sample's own fields are checked by
 * Validate; how a backend queues and hands batches over stays with the backend.
 */
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

export module lightphi.input:batch_validation;

import :capabilities;
import :samples;
import :source;
import :validation;

export namespace lightphi::input
{
    /** {brief} Why one delivery breaks the delivery rules. */
    enum class BatchValidationReason : std::uint8_t
    {
        ZeroContact,           ///< The batch names no contact.
        UndefinedPhase,        ///< The contact phase value is not defined.
        TooManySamples,        ///< The batch exceeds the source's or the contract's sample bound.
        MissingSamples,        ///< A Begin or Update carries no sample.
        SamplesOnCancel,       ///< A Cancel carries samples.
        ContactAlreadyOpen,    ///< A Begin arrives while another contact is open.
        ContactNotOpen,        ///< An Update, End or Cancel names a contact that is not open.
        OrdinalNotIncreasing,  ///< The batch ordinal does not increase within the contact.
        InvalidSample,         ///< One sample fails Validate; the sample error says why.
        IdentityNotIncreasing, ///< A sample identity does not increase within the contact.
        SequenceOutOfOrder,    ///< A committed sample does not advance the contact's sequence.
        TimeNotMonotonic,      ///< A reported timestamp goes backwards within the contact.
        UnknownCorrection,     ///< A corrected sample names no prediction of the preceding batch.
        RepeatedCorrection,    ///< Two corrected samples in one batch replace the same prediction.
        PredictionNotAhead     ///< A prediction does not lie ahead of every committed and earlier predicted sample.
    };

    /** {brief} Actionable detail for one rejected delivery. */
    struct BatchValidationError
    {
        BatchValidationReason Reason{};      ///< Why the delivery was rejected.
        std::uint32_t         SampleIndex{}; ///< Offending sample, or zero for a batch-level reason.
        SampleValidationError Sample{};      ///< Why the sample failed, when Reason is InvalidSample.

        friend constexpr bool operator==(const BatchValidationError &, const BatchValidationError &) = default;
    };

    /** {brief} Human-readable explanation for one delivery validation reason. */
    [[nodiscard]] constexpr std::string_view BatchValidationMessage(BatchValidationReason reason) noexcept
    {
        switch (reason)
        {
            case BatchValidationReason::ZeroContact:
                return "the batch names no contact";
            case BatchValidationReason::UndefinedPhase:
                return "the contact phase is not defined";
            case BatchValidationReason::TooManySamples:
                return "the batch exceeds the sample bound";
            case BatchValidationReason::MissingSamples:
                return "a Begin or Update batch carries no sample";
            case BatchValidationReason::SamplesOnCancel:
                return "a Cancel batch carries samples";
            case BatchValidationReason::ContactAlreadyOpen:
                return "a contact begins while another is open";
            case BatchValidationReason::ContactNotOpen:
                return "the batch names a contact that is not open";
            case BatchValidationReason::OrdinalNotIncreasing:
                return "the batch ordinal does not increase within the contact";
            case BatchValidationReason::InvalidSample:
                return "a sample is invalid";
            case BatchValidationReason::IdentityNotIncreasing:
                return "a sample identity does not increase within the contact";
            case BatchValidationReason::SequenceOutOfOrder:
                return "a committed sample does not advance the contact sequence";
            case BatchValidationReason::TimeNotMonotonic:
                return "a timestamp goes backwards within the contact";
            case BatchValidationReason::UnknownCorrection:
                return "a corrected sample names no prediction of the preceding batch";
            case BatchValidationReason::RepeatedCorrection:
                return "two corrected samples replace the same prediction";
            case BatchValidationReason::PredictionNotAhead:
                return "a prediction does not lie ahead of the committed samples";
        }
        return "the batch is invalid";
    }

    /**
     * {brief} What the deliveries accepted so far establish for the next one.
     *
     * Start from a default value, and keep the state ValidateBatch returns only for a delivery the
     * receiver accepted: a busy receiver will be offered the identical batch again.
     */
    struct DeliveryState
    {
        bool          ContactOpen{};             ///< Whether a contact is open.
        ContactId     Contact{};                 ///< The open contact, or zero.
        std::uint64_t LastBatchOrdinal{};        ///< Ordinal of the last delivery in the open contact.
        std::uint64_t LastSampleId{};            ///< Highest sample identity in the open contact.
        std::uint64_t LastCommittedSequence{};   ///< Highest measured, coalesced or corrected sequence.
        std::uint64_t LastTimestamp{};           ///< Latest reported timestamp in the open contact.
        std::uint32_t PreviousPredictionCount{}; ///< Predictions in the last delivery, which may be corrected.
        std::array<SampleId, MaximumBatchSamples>      PreviousPredictionIds{};       ///< Their identities.
        std::array<std::uint64_t, MaximumBatchSamples> PreviousPredictionSequences{}; ///< Their sequences.
    };
} // namespace lightphi::input

// Not exported: the steps ValidateBatch takes, which no caller names. The exported inline
// ValidateBatch calls them, so they need module linkage; internal linkage would be ill-formed, and
// the check does not see module linkage.
// NOLINTBEGIN(misc-use-internal-linkage)
namespace lightphi::input::detail
{
    /** {brief} Running totals accumulated while checking the samples of one batch. */
    struct BatchScan
    {
        std::uint64_t SampleId{};
        std::uint64_t CommittedSequence{};
        std::uint64_t Timestamp{};
        std::uint32_t PredictionCount{};
    };

    [[nodiscard]] constexpr std::expected<void, BatchValidationError> Fail(BatchValidationReason reason,
                                                                           std::size_t           index = 0U) noexcept
    {
        return std::unexpected{
            BatchValidationError{.Reason = reason, .SampleIndex = static_cast<std::uint32_t>(index)}};
    }

    [[nodiscard]] inline std::expected<void, BatchValidationError>
    ValidateEnvelope(const InputBatch &batch, const InputSourceCapabilities &capabilities) noexcept
    {
        if (batch.Contact.Value == 0U)
        {
            return Fail(BatchValidationReason::ZeroContact);
        }
        if (batch.Phase > LastContactPhase)
        {
            return Fail(BatchValidationReason::UndefinedPhase);
        }
        if (batch.Samples.size() > capabilities.MaximumBatchSamples || batch.Samples.size() > MaximumBatchSamples)
        {
            return Fail(BatchValidationReason::TooManySamples);
        }
        if ((batch.Phase == ContactPhase::Begin || batch.Phase == ContactPhase::Update) && batch.Samples.empty())
        {
            return Fail(BatchValidationReason::MissingSamples);
        }
        if (batch.Phase == ContactPhase::Cancel && !batch.Samples.empty())
        {
            return Fail(BatchValidationReason::SamplesOnCancel);
        }
        return {};
    }

    [[nodiscard]] inline bool MatchesPreviousPrediction(const DeliveryState &state, const InputSample &sample) noexcept
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

    /** {brief} A corrected sample names a prediction of the preceding batch, and only once. */
    [[nodiscard]] inline std::expected<void, BatchValidationError> ValidateCorrected(const DeliveryState &previous,
                                                                                     const InputBatch    &batch,
                                                                                     std::size_t          index,
                                                                                     BatchScan           &scan) noexcept
    {
        const InputSample &sample{batch.Samples[index]};
        if (!MatchesPreviousPrediction(previous, sample))
        {
            return Fail(BatchValidationReason::UnknownCorrection, index);
        }
        for (std::size_t earlier{}; earlier < index; ++earlier)
        {
            if (batch.Samples[earlier].Origin == SampleOrigin::Corrected &&
                batch.Samples[earlier].Corrects == sample.Corrects)
            {
                return Fail(BatchValidationReason::RepeatedCorrection, index);
            }
        }
        scan.CommittedSequence = std::max(scan.CommittedSequence, sample.Sequence);
        return {};
    }

    /** {brief} A prediction lies ahead of everything before it, and is kept for correction. */
    [[nodiscard]] inline std::expected<void, BatchValidationError>
    ValidatePredicted(const InputSample &sample, std::size_t index, BatchScan &scan, std::span<SampleId> predictionIds,
                      std::span<std::uint64_t> predictionSequences) noexcept
    {
        if (sample.Sequence <= scan.CommittedSequence ||
            (scan.PredictionCount > 0U && sample.Sequence <= predictionSequences[scan.PredictionCount - 1U]))
        {
            return Fail(BatchValidationReason::PredictionNotAhead, index);
        }
        predictionIds[scan.PredictionCount]       = sample.Id;
        predictionSequences[scan.PredictionCount] = sample.Sequence;
        ++scan.PredictionCount;
        return {};
    }

    /** {brief} A measured or coalesced sample advances the committed sequence. */
    [[nodiscard]] inline std::expected<void, BatchValidationError>
    ValidateObserved(const InputSample &sample, std::size_t index, BatchScan &scan) noexcept
    {
        if (sample.Sequence <= scan.CommittedSequence)
        {
            return Fail(BatchValidationReason::SequenceOutOfOrder, index);
        }
        scan.CommittedSequence = sample.Sequence;
        return {};
    }

    [[nodiscard]] inline std::expected<void, BatchValidationError>
    ValidateSamples(const InputBatch &batch, const InputSourceCapabilities &capabilities, const DeliveryState &previous,
                    BatchScan &scan, std::span<SampleId> predictionIds,
                    std::span<std::uint64_t> predictionSequences) noexcept
    {
        std::uint64_t lastSequenceInBatch{};
        for (std::size_t index{}; index < batch.Samples.size(); ++index)
        {
            const InputSample &sample{batch.Samples[index]};
            if (const auto validity{Validate(sample, capabilities)}; !validity)
            {
                return std::unexpected{BatchValidationError{.Reason      = BatchValidationReason::InvalidSample,
                                                            .SampleIndex = static_cast<std::uint32_t>(index),
                                                            .Sample      = validity.error()}};
            }
            if (sample.Id.Value <= scan.SampleId)
            {
                return Fail(BatchValidationReason::IdentityNotIncreasing, index);
            }
            if (index > 0U && sample.Sequence < lastSequenceInBatch)
            {
                return Fail(BatchValidationReason::SequenceOutOfOrder, index);
            }
            if (Supports(capabilities, InputCapability::Timestamp) && sample.TimeNanoseconds < scan.Timestamp)
            {
                return Fail(BatchValidationReason::TimeNotMonotonic, index);
            }
            scan.SampleId  = sample.Id.Value;
            scan.Timestamp = std::max(scan.Timestamp, sample.TimeNanoseconds);

            std::expected<void, BatchValidationError> checked{};
            switch (sample.Origin)
            {
                case SampleOrigin::Corrected:
                    checked = ValidateCorrected(previous, batch, index, scan);
                    break;
                case SampleOrigin::Predicted:
                    checked = ValidatePredicted(sample, index, scan, predictionIds, predictionSequences);
                    break;
                case SampleOrigin::Measured:
                case SampleOrigin::Coalesced:
                    checked = ValidateObserved(sample, index, scan);
                    break;
            }
            if (!checked)
            {
                return checked;
            }
            lastSequenceInBatch = sample.Sequence;
        }
        return {};
    }
} // namespace lightphi::input::detail
// NOLINTEND(misc-use-internal-linkage)

export namespace lightphi::input
{
    /**
     * {brief} Check one delivery against the delivery rules and return the state it leaves.
     *
     * A source can use it to refuse a delivery no device could produce, and a test or a receiver can
     * use it to check that a real source never sends one.
     *
     * ```cpp
     * DeliveryState state{};
     * if (const auto next{ValidateBatch(batch, source.Capabilities(), state)}; next)
     * {
     *     state = *next; // only once the receiver has accepted the batch
     * }
     * ```
     */
    [[nodiscard]] inline std::expected<DeliveryState, BatchValidationError>
    ValidateBatch(const InputBatch &batch, const InputSourceCapabilities &capabilities,
                  const DeliveryState &state) noexcept
    {
        if (const auto envelope{detail::ValidateEnvelope(batch, capabilities)}; !envelope)
        {
            return std::unexpected{envelope.error()};
        }

        DeliveryState next{state};
        if (batch.Phase == ContactPhase::Begin)
        {
            if (next.ContactOpen)
            {
                return std::unexpected{BatchValidationError{.Reason = BatchValidationReason::ContactAlreadyOpen}};
            }
            next = DeliveryState{.ContactOpen = true, .Contact = batch.Contact, .LastBatchOrdinal = batch.BatchOrdinal};
        }
        else if (!next.ContactOpen || next.Contact != batch.Contact)
        {
            return std::unexpected{BatchValidationError{.Reason = BatchValidationReason::ContactNotOpen}};
        }
        else if (batch.BatchOrdinal <= next.LastBatchOrdinal)
        {
            return std::unexpected{BatchValidationError{.Reason = BatchValidationReason::OrdinalNotIncreasing}};
        }

        std::array<SampleId, MaximumBatchSamples>      predictionIds{};
        std::array<std::uint64_t, MaximumBatchSamples> predictionSequences{};
        detail::BatchScan                              scan{.SampleId          = next.LastSampleId,
                                                            .CommittedSequence = next.LastCommittedSequence,
                                                            .Timestamp         = next.LastTimestamp};
        if (const auto samples{
                detail::ValidateSamples(batch, capabilities, next, scan, predictionIds, predictionSequences)};
            !samples)
        {
            return std::unexpected{samples.error()};
        }

        if (batch.Phase == ContactPhase::End || batch.Phase == ContactPhase::Cancel)
        {
            return DeliveryState{};
        }
        next.LastBatchOrdinal            = batch.BatchOrdinal;
        next.LastSampleId                = scan.SampleId;
        next.LastCommittedSequence       = scan.CommittedSequence;
        next.LastTimestamp               = scan.Timestamp;
        next.PreviousPredictionCount     = scan.PredictionCount;
        next.PreviousPredictionIds       = predictionIds;
        next.PreviousPredictionSequences = predictionSequences;
        return next;
    }
} // namespace lightphi::input
