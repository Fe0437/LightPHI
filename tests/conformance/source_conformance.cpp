/**
 * {file}
 * {brief} Runs the LightPHI source contract against the deterministic fake backend.
 *
 * No Flexible Drawing code or operating-system input is used.
 */
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>

import lightphi.input;
import lightphi.backend.fake;

namespace {
using namespace lightphi::input;
using namespace lightphi::input::fake;

int Failures{};

#define LP_CHECK(condition)                                                                                            \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n';                         \
            ++Failures;                                                                                                \
        }                                                                                                              \
    } while (false)

class Receiver final : public IInputReceiver {
  public:
    [[nodiscard]] DeliveryResult Receive(const InputBatch &batch) noexcept override {
        if (BusyOnce) {
            BusyOnce = false;
            BusyContact = batch.Contact;
            BusyOrdinal = batch.BatchOrdinal;
            BusyPhase = batch.Phase;
            BusySampleData = batch.Samples.data();
            BusySampleCount = batch.Samples.size();
            return DeliveryResult::Busy;
        }
        if (BusySampleData != nullptr) {
            LP_CHECK(batch.Contact == BusyContact);
            LP_CHECK(batch.BatchOrdinal == BusyOrdinal);
            LP_CHECK(batch.Phase == BusyPhase);
            LP_CHECK(batch.Samples.data() == BusySampleData);
            LP_CHECK(batch.Samples.size() == BusySampleCount);
            BusySampleData = nullptr;
        }
        if (Reject) {
            return DeliveryResult::Rejected;
        }
        LP_CHECK(Count < Batches.size());
        if (Count >= Batches.size()) {
            return DeliveryResult::Rejected;
        }
        Batches[Count] = batch;
        for (std::size_t index{}; index < batch.Samples.size(); ++index) {
            Samples[Count][index] = batch.Samples[index];
        }
        Batches[Count].Samples = {Samples[Count].data(), batch.Samples.size()};
        ++Count;
        return DeliveryResult::Accepted;
    }

    void SourceFailed(const InputSourceError error) noexcept override {
        ++ErrorCount;
        LastError = error;
    }

    std::array<InputBatch, 8> Batches{};
    std::array<std::array<InputSample, MaximumBatchSamples>, 8> Samples{};
    std::uint32_t Count{};
    std::uint32_t ErrorCount{};
    InputSourceError LastError{};
    bool BusyOnce{};
    bool Reject{};
    ContactId BusyContact{};
    std::uint64_t BusyOrdinal{};
    ContactPhase BusyPhase{};
    const InputSample *BusySampleData{};
    std::size_t BusySampleCount{};
};

[[nodiscard]] InputSourceCapabilities FullCapabilities(const std::uint32_t maximumSamples = 4U) {
    return {.Pressure = true,
            .Coalescing = true,
            .Prediction = true,
            .Correction = true,
            .Timestamp = true,
            .Tilt = true,
            .Twist = true,
            .Eraser = true,
            .Hover = true,
            .MaximumBatchSamples = maximumSamples};
}

[[nodiscard]] InputSample Sample(const std::uint64_t id, const std::uint64_t sequence, const std::uint64_t timestamp,
                                 const SampleOrigin origin = SampleOrigin::Measured) {
    return {.Id = {.Value = id},
            .Corrects = {},
            .Sequence = sequence,
            .TimeNanoseconds = timestamp,
            .X = static_cast<double>(sequence),
            .Y = static_cast<double>(sequence) + 0.5,
            .Pressure = 0.5,
            .TiltXRadians = 0.1,
            .TiltYRadians = -0.1,
            .TwistRadians = 0.25,
            .Origin = origin,
            .Eraser = false,
            .Hovering = false};
}

[[nodiscard]] InputBatch Batch(const std::uint64_t contact, const std::uint64_t ordinal, const ContactPhase phase,
                               const std::span<const InputSample> samples = {}) {
    return {.Contact = {.Value = contact}, .BatchOrdinal = ordinal, .Phase = phase, .Samples = samples};
}

[[nodiscard]] std::unique_ptr<FakeInputSource> Create(const InputSourceCapabilities capabilities = FullCapabilities(),
                                                      const std::uint32_t queueCapacity = 4U) {
    auto result{CreateFakeInputSource({.Capabilities = capabilities, .MaximumQueuedBatches = queueCapacity})};
    LP_CHECK(result.has_value());
    return result ? std::move(*result) : nullptr;
}

void TestLifecycleAndCapabilities() {
    auto source{Create()};
    LP_CHECK(source != nullptr);
    if (!source) {
        return;
    }
    LP_CHECK(source->Capabilities() == FullCapabilities());
    LP_CHECK(source->DispatchNext().error() == DispatchError::NotStarted);
    Receiver receiver{};
    LP_CHECK(source->Start(receiver).has_value());
    LP_CHECK(source->Start(receiver).error() == InputSourceError::AlreadyStarted);
    source->Stop();
    source->Stop();
    LP_CHECK(source->DispatchNext().error() == DispatchError::NotStarted);
    LP_CHECK(source->Start(receiver).has_value());
}

void TestOrderedCorrectionAndEnd() {
    auto source{Create()};
    Receiver receiver{};
    LP_CHECK(source->Start(receiver).has_value());

    std::array beginSamples{Sample(10U, 1U, 100U), Sample(11U, 2U, 110U, SampleOrigin::Predicted)};
    LP_CHECK(source->Enqueue(Batch(7U, 1U, ContactPhase::Begin, beginSamples)).has_value());

    auto correction{Sample(12U, 2U, 120U, SampleOrigin::Corrected)};
    correction.Corrects = {.Value = 11U};
    auto coalesced{Sample(13U, 3U, 130U, SampleOrigin::Coalesced)};
    std::array updateSamples{correction, coalesced};
    auto wrongCorrectionSamples{updateSamples};
    wrongCorrectionSamples[0].Corrects = {.Value = 999U};
    LP_CHECK(source->Enqueue(Batch(7U, 2U, ContactPhase::Update, wrongCorrectionSamples)).error() ==
             FakeSourceError::InvalidBatch);
    std::array duplicateCorrections{correction, correction};
    duplicateCorrections[1].Id = {.Value = 13U};
    LP_CHECK(source->Enqueue(Batch(7U, 2U, ContactPhase::Update, duplicateCorrections)).error() ==
             FakeSourceError::InvalidBatch);
    LP_CHECK(source->Enqueue(Batch(7U, 2U, ContactPhase::Update, updateSamples)).has_value());
    LP_CHECK(source->Enqueue(Batch(7U, 3U, ContactPhase::End)).has_value());

    LP_CHECK(source->DispatchNext().has_value());
    LP_CHECK(source->DispatchNext().has_value());
    LP_CHECK(source->DispatchNext().has_value());
    LP_CHECK(source->DispatchNext().error() == DispatchError::QueueEmpty);
    LP_CHECK(receiver.Count == 3U);
    LP_CHECK(receiver.Batches[0].Phase == ContactPhase::Begin);
    LP_CHECK(receiver.Batches[1].Samples[0].Origin == SampleOrigin::Corrected);
    LP_CHECK(receiver.Batches[1].Samples[0].Corrects == SampleId{.Value = 11U});
    LP_CHECK(receiver.Batches[1].Samples[0].Sequence == 2U);
    LP_CHECK(receiver.Batches[2].Phase == ContactPhase::End);
}

void TestCancellation() {
    auto source{Create()};
    Receiver receiver{};
    LP_CHECK(source->Start(receiver).has_value());
    std::array samples{Sample(20U, 1U, 200U)};
    LP_CHECK(source->Enqueue(Batch(8U, 1U, ContactPhase::Begin, samples)).has_value());
    LP_CHECK(source->Enqueue(Batch(8U, 2U, ContactPhase::Cancel)).has_value());
    LP_CHECK(source->DispatchNext().has_value());
    LP_CHECK(source->DispatchNext().has_value());
    LP_CHECK(receiver.Count == 2U);
    LP_CHECK(receiver.Batches[1].Phase == ContactPhase::Cancel);
    LP_CHECK(receiver.Batches[1].Samples.empty());
}

void TestBoundsAndCapabilities() {
    auto source{Create(FullCapabilities(2U), 1U)};
    std::array tooMany{Sample(30U, 1U, 300U), Sample(31U, 2U, 310U), Sample(32U, 3U, 320U)};
    LP_CHECK(source->Enqueue(Batch(9U, 1U, ContactPhase::Begin, tooMany)).error() == FakeSourceError::BatchTooLarge);

    std::array valid{Sample(33U, 1U, 330U)};
    LP_CHECK(source->Enqueue(Batch(9U, 1U, ContactPhase::Begin, valid)).has_value());
    LP_CHECK(source->Enqueue(Batch(9U, 2U, ContactPhase::End)).error() == FakeSourceError::QueueFull);

    InputSourceCapabilities timestampOnly{.Timestamp = true, .MaximumBatchSamples = 2U};
    auto limitedSource{Create(timestampOnly)};
    auto undeclared{Sample(40U, 1U, 400U)};
    std::array limitedSamples{undeclared};
    LP_CHECK(limitedSource->Enqueue(Batch(10U, 1U, ContactPhase::Begin, limitedSamples)).error() ==
             FakeSourceError::UndeclaredValue);
    limitedSamples[0].Pressure = 0.0;
    limitedSamples[0].TiltXRadians = 0.0;
    limitedSamples[0].TiltYRadians = 0.0;
    limitedSamples[0].TwistRadians = 0.0;
    LP_CHECK(limitedSource->Enqueue(Batch(10U, 1U, ContactPhase::Begin, limitedSamples)).has_value());
}

void TestBackpressureAndFailure() {
    auto source{Create()};
    Receiver receiver{};
    receiver.BusyOnce = true;
    LP_CHECK(source->Start(receiver).has_value());
    std::array samples{Sample(50U, 1U, 500U)};
    LP_CHECK(source->Enqueue(Batch(11U, 1U, ContactPhase::Begin, samples)).has_value());
    LP_CHECK(source->DispatchNext().error() == DispatchError::ReceiverBusy);
    LP_CHECK(source->QueuedBatchCount() == 1U);
    LP_CHECK(receiver.Count == 0U);
    LP_CHECK(source->DispatchNext().has_value());
    LP_CHECK(receiver.Count == 1U);

    source->FailNext();
    LP_CHECK(source->DispatchNext().error() == DispatchError::BackendFailure);
    LP_CHECK(receiver.ErrorCount == 1U);
    LP_CHECK(receiver.LastError == InputSourceError::BackendFailure);
    LP_CHECK(source->DispatchNext().error() == DispatchError::NotStarted);
}

void TestReceiverRejection() {
    auto source{Create()};
    Receiver receiver{};
    receiver.Reject = true;
    LP_CHECK(source->Start(receiver).has_value());
    std::array samples{Sample(60U, 1U, 600U)};
    LP_CHECK(source->Enqueue(Batch(12U, 1U, ContactPhase::Begin, samples)).has_value());
    LP_CHECK(source->DispatchNext().error() == DispatchError::ReceiverRejected);
    LP_CHECK(source->QueuedBatchCount() == 0U);

    receiver.Reject = false;
    std::array restartedSamples{Sample(61U, 1U, 610U)};
    LP_CHECK(source->Enqueue(Batch(13U, 1U, ContactPhase::Begin, restartedSamples)).has_value());
    LP_CHECK(source->DispatchNext().has_value());
    LP_CHECK(receiver.Count == 1U);
}

void TestQueueWraparound() {
    auto source{Create(FullCapabilities(), 2U)};
    Receiver receiver{};
    LP_CHECK(source->Start(receiver).has_value());

    std::array firstSamples{Sample(70U, 1U, 700U)};
    LP_CHECK(source->Enqueue(Batch(14U, 1U, ContactPhase::Begin, firstSamples)).has_value());
    LP_CHECK(source->Enqueue(Batch(14U, 2U, ContactPhase::End)).has_value());
    LP_CHECK(source->DispatchNext().has_value());

    std::array secondSamples{Sample(71U, 1U, 710U)};
    LP_CHECK(source->Enqueue(Batch(15U, 1U, ContactPhase::Begin, secondSamples)).has_value());
    LP_CHECK(source->DispatchNext().has_value());
    LP_CHECK(source->DispatchNext().has_value());
    LP_CHECK(receiver.Count == 3U);
    LP_CHECK(receiver.Batches[2].Contact == ContactId{.Value = 15U});
}

void TestDescriptorValidation() {
    LP_CHECK(CreateFakeInputSource({}).error() == FakeSourceError::InvalidDescriptor);
    LP_CHECK(
        CreateFakeInputSource({.Capabilities = FullCapabilities(MaximumBatchSamples + 1U), .MaximumQueuedBatches = 1U})
            .error() == FakeSourceError::InvalidDescriptor);

    auto correctionWithoutPrediction{FullCapabilities()};
    correctionWithoutPrediction.Prediction = false;
    LP_CHECK(CreateFakeInputSource({.Capabilities = correctionWithoutPrediction, .MaximumQueuedBatches = 1U}).error() ==
             FakeSourceError::InvalidDescriptor);
}
} // namespace

int main() {
    TestLifecycleAndCapabilities();
    TestOrderedCorrectionAndEnd();
    TestCancellation();
    TestBoundsAndCapabilities();
    TestBackpressureAndFailure();
    TestReceiverRejection();
    TestQueueWraparound();
    TestDescriptorValidation();
    return Failures == 0 ? 0 : 1;
}
