/**
 * {file}
 * {brief} Proves ValidateBatch names the exact delivery rule each impossible batch breaks.
 *
 * It sees lightphi.input only. Whether a particular backend obeys the rules is checked where that
 * backend is tested.
 */
#include <array>
#include <cstdint>
#include <expected>
#include <span>

import lightphi.input;

using namespace lightphi::input;

namespace
{
    constexpr InputSourceCapabilities Capabilities{.Available = InputCapability::Pressure | InputCapability::Timestamp |
                                                                InputCapability::Prediction |
                                                                InputCapability::Correction,
                                                   .MaximumBatchSamples = 4U};

    [[nodiscard]] InputSample Sample(std::uint64_t id, std::uint64_t sequence, std::uint64_t time,
                                     SampleOrigin origin = SampleOrigin::Measured, std::uint64_t corrects = 0U) noexcept
    {
        return {.Id              = {.Value = id},
                .Corrects        = {.Value = corrects},
                .Sequence        = sequence,
                .TimeNanoseconds = time,
                .Pressure        = 0.5,
                .Origin          = origin};
    }

    [[nodiscard]] InputBatch Batch(std::uint64_t contact, std::uint64_t ordinal, ContactPhase phase,
                                   std::span<const InputSample> samples) noexcept
    {
        return {.Contact = {.Value = contact}, .BatchOrdinal = ordinal, .Phase = phase, .Samples = samples};
    }

    /** {brief} Whether the batch is refused for exactly this reason. */
    [[nodiscard]] bool Refuses(const InputBatch &batch, const DeliveryState &state,
                               BatchValidationReason reason) noexcept
    {
        const auto result{ValidateBatch(batch, Capabilities, state)};
        return !result && result.error().Reason == reason;
    }

    /** {brief} The state one accepted batch leaves, or a closed state when it was refused. */
    [[nodiscard]] DeliveryState After(const InputBatch &batch, const DeliveryState &state) noexcept
    {
        return ValidateBatch(batch, Capabilities, state).value_or(DeliveryState{});
    }

    [[nodiscard]] bool RefusesBrokenEnvelopes() noexcept
    {
        const std::array<InputSample, 1> one{Sample(1U, 1U, 10U)};
        const std::array<InputSample, 5> five{Sample(1U, 1U, 10U), Sample(2U, 2U, 20U), Sample(3U, 3U, 30U),
                                              Sample(4U, 4U, 40U), Sample(5U, 5U, 50U)};
        const DeliveryState              closed{};
        return Refuses(Batch(0U, 1U, ContactPhase::Begin, one), closed, BatchValidationReason::ZeroContact) &&
               Refuses(Batch(1U, 1U, static_cast<ContactPhase>(9U), one), closed,
                       BatchValidationReason::UndefinedPhase) &&
               Refuses(Batch(1U, 1U, ContactPhase::Begin, five), closed, BatchValidationReason::TooManySamples) &&
               Refuses(Batch(1U, 1U, ContactPhase::Begin, {}), closed, BatchValidationReason::MissingSamples) &&
               Refuses(Batch(1U, 1U, ContactPhase::Cancel, one), closed, BatchValidationReason::SamplesOnCancel);
    }

    [[nodiscard]] bool FollowsTheContactLifecycle() noexcept
    {
        const std::array<InputSample, 1> first{Sample(1U, 1U, 10U)};
        const std::array<InputSample, 1> second{Sample(2U, 2U, 20U)};
        const DeliveryState              open{After(Batch(1U, 1U, ContactPhase::Begin, first), {})};
        if (!open.ContactOpen ||
            !Refuses(Batch(1U, 1U, ContactPhase::Update, second), {}, BatchValidationReason::ContactNotOpen) ||
            !Refuses(Batch(2U, 2U, ContactPhase::Begin, second), open, BatchValidationReason::ContactAlreadyOpen) ||
            !Refuses(Batch(2U, 2U, ContactPhase::Update, second), open, BatchValidationReason::ContactNotOpen) ||
            !Refuses(Batch(1U, 1U, ContactPhase::Update, second), open, BatchValidationReason::OrdinalNotIncreasing))
        {
            return false;
        }
        // End and Cancel both close the contact, so the next Begin is legal again.
        const DeliveryState ended{After(Batch(1U, 2U, ContactPhase::End, second), open)};
        const DeliveryState cancelled{After(Batch(1U, 2U, ContactPhase::Cancel, {}), open)};
        return !ended.ContactOpen && !cancelled.ContactOpen &&
               ValidateBatch(Batch(2U, 1U, ContactPhase::Begin, first), Capabilities, ended).has_value();
    }

    [[nodiscard]] bool RefusesMisorderedSamples() noexcept
    {
        const std::array<InputSample, 2> repeatedId{Sample(1U, 1U, 10U), Sample(1U, 2U, 20U)};
        const std::array<InputSample, 2> backwardSequence{Sample(1U, 5U, 10U), Sample(2U, 4U, 20U)};
        const std::array<InputSample, 2> backwardTime{Sample(1U, 1U, 30U), Sample(2U, 2U, 20U)};
        const std::array<InputSample, 1> undeclaredTilt{InputSample{.Id = {.Value = 1U}, .TiltXRadians = 0.5}};
        const DeliveryState              closed{};
        const auto invalid{ValidateBatch(Batch(1U, 1U, ContactPhase::Begin, undeclaredTilt), Capabilities, closed)};
        return Refuses(Batch(1U, 1U, ContactPhase::Begin, repeatedId), closed,
                       BatchValidationReason::IdentityNotIncreasing) &&
               Refuses(Batch(1U, 1U, ContactPhase::Begin, backwardSequence), closed,
                       BatchValidationReason::SequenceOutOfOrder) &&
               Refuses(Batch(1U, 1U, ContactPhase::Begin, backwardTime), closed,
                       BatchValidationReason::TimeNotMonotonic) &&
               !invalid && invalid.error().Reason == BatchValidationReason::InvalidSample &&
               invalid.error().Sample.Reason == SampleValidationReason::MissingCapability;
    }

    [[nodiscard]] bool FollowsPredictionAndCorrection() noexcept
    {
        const std::array<InputSample, 3> opening{Sample(1U, 1U, 10U), Sample(2U, 2U, 20U, SampleOrigin::Predicted),
                                                 Sample(3U, 3U, 30U, SampleOrigin::Predicted)};
        const DeliveryState              open{After(Batch(1U, 1U, ContactPhase::Begin, opening), {})};
        const std::array<InputSample, 1> unknown{Sample(4U, 2U, 40U, SampleOrigin::Corrected, 99U)};
        const std::array<InputSample, 2> repeated{Sample(4U, 2U, 40U, SampleOrigin::Corrected, 2U),
                                                  Sample(5U, 2U, 41U, SampleOrigin::Corrected, 2U)};
        const std::array<InputSample, 1> behind{Sample(4U, 1U, 40U, SampleOrigin::Predicted)};
        const std::array<InputSample, 1> corrected{Sample(4U, 2U, 40U, SampleOrigin::Corrected, 2U)};
        const DeliveryState              afterCorrection{After(Batch(1U, 2U, ContactPhase::Update, corrected), open)};
        // Predictions of the first batch cannot be corrected once a second batch has replaced them.
        const std::array<InputSample, 1> stale{Sample(5U, 3U, 50U, SampleOrigin::Corrected, 3U)};
        return open.PreviousPredictionCount == 2U &&
               Refuses(Batch(1U, 2U, ContactPhase::Update, unknown), open, BatchValidationReason::UnknownCorrection) &&
               Refuses(Batch(1U, 2U, ContactPhase::Update, repeated), open,
                       BatchValidationReason::RepeatedCorrection) &&
               Refuses(Batch(1U, 2U, ContactPhase::Update, behind), open, BatchValidationReason::PredictionNotAhead) &&
               afterCorrection.ContactOpen &&
               Refuses(Batch(1U, 3U, ContactPhase::Update, stale), afterCorrection,
                       BatchValidationReason::UnknownCorrection);
    }
} // namespace

int main()
{
    return RefusesBrokenEnvelopes() && FollowsTheContactLifecycle() && RefusesMisorderedSamples() &&
                   FollowsPredictionAndCorrection() &&
                   !BatchValidationMessage(BatchValidationReason::ZeroContact).empty()
               ? 0
               : 1;
}
