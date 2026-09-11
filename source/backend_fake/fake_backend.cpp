/**
 * {file}
 * {brief} Implements the deterministic preallocated LightPHI input backend.
 *
 * No platform event type, thread, timer, or drawing behavior is present here.
 */
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <new>

module lightphi.backend.fake;

namespace lightphi::input::fake {
namespace {
struct BatchSlot {
    ContactId Contact{};
    std::uint64_t BatchOrdinal{};
    ContactPhase Phase{};
    InputSample *Samples{};
    std::uint32_t SampleCount{};
};

struct ScriptState {
    bool ContactOpen{};
    ContactId Contact{};
    std::uint64_t LastBatchOrdinal{};
    std::uint64_t LastSampleId{};
    std::uint64_t LastCommittedSequence{};
    std::uint64_t LastTimestamp{};
    std::uint32_t PreviousPredictionCount{};
    std::array<SampleId, MaximumBatchSamples> PreviousPredictionIds{};
    std::array<std::uint64_t, MaximumBatchSamples> PreviousPredictionSequences{};
};

[[nodiscard]] bool MatchesPreviousPrediction(const ScriptState &state, const InputSample &sample) noexcept {
    for (std::uint32_t index{}; index < state.PreviousPredictionCount; ++index) {
        if (state.PreviousPredictionIds[index] == sample.Corrects &&
            state.PreviousPredictionSequences[index] == sample.Sequence) {
            return true;
        }
    }
    return false;
}
} // namespace

class FakeInputSource::Impl {
  public:
    InputSourceCapabilities Capabilities{};
    std::uint32_t QueueCapacity{};
    std::uint32_t QueueHead{};
    std::uint32_t QueueSize{};
    std::unique_ptr<BatchSlot[]> Slots{};
    std::unique_ptr<InputSample[]> SampleStorage{};
    IInputReceiver *Receiver{};
    bool FailNext{};
    ScriptState Script{};

    void ResetDelivery() noexcept {
        QueueHead = 0U;
        QueueSize = 0U;
        Receiver = nullptr;
        FailNext = false;
        Script = {};
    }

    [[nodiscard]] std::expected<ScriptState, FakeSourceError> Validate(const InputBatch &batch) const noexcept {
        ScriptState next{Script};
        if (batch.Contact.Value == 0U || batch.Phase > LastContactPhase) {
            return std::unexpected{FakeSourceError::InvalidBatch};
        }
        if (batch.Samples.size() > Capabilities.MaximumBatchSamples || batch.Samples.size() > MaximumBatchSamples) {
            return std::unexpected{FakeSourceError::BatchTooLarge};
        }
        const bool terminal{batch.Phase == ContactPhase::End || batch.Phase == ContactPhase::Cancel};
        if ((batch.Phase == ContactPhase::Begin || batch.Phase == ContactPhase::Update) && batch.Samples.empty()) {
            return std::unexpected{FakeSourceError::InvalidBatch};
        }
        if (batch.Phase == ContactPhase::Cancel && !batch.Samples.empty()) {
            return std::unexpected{FakeSourceError::InvalidBatch};
        }
        if (batch.Phase == ContactPhase::Begin) {
            if (next.ContactOpen) {
                return std::unexpected{FakeSourceError::InvalidBatch};
            }
            next.ContactOpen = true;
            next.Contact = batch.Contact;
            next.LastBatchOrdinal = batch.BatchOrdinal;
            next.LastSampleId = 0U;
            next.LastCommittedSequence = 0U;
            next.LastTimestamp = 0U;
            next.PreviousPredictionCount = 0U;
        } else if (!next.ContactOpen || next.Contact != batch.Contact || batch.BatchOrdinal <= next.LastBatchOrdinal) {
            return std::unexpected{FakeSourceError::InvalidBatch};
        }

        std::uint64_t sampleId{next.LastSampleId};
        std::uint64_t committedSequence{next.LastCommittedSequence};
        std::uint64_t timestamp{next.LastTimestamp};
        std::uint64_t lastSequenceInBatch{};
        std::uint32_t predictionCount{};
        std::array<SampleId, MaximumBatchSamples> predictionIds{};
        std::array<std::uint64_t, MaximumBatchSamples> predictionSequences{};
        for (std::size_t index{}; index < batch.Samples.size(); ++index) {
            const InputSample &sample{batch.Samples[index]};
            if (const auto validity{input::Validate(sample, Capabilities)}; !validity) {
                return std::unexpected{validity.error() == SampleError::UndeclaredValue
                                           ? FakeSourceError::UndeclaredValue
                                           : FakeSourceError::InvalidBatch};
            }
            if (sample.Id.Value <= sampleId || (index > 0U && sample.Sequence < lastSequenceInBatch) ||
                (Capabilities.Timestamp && sample.TimeNanoseconds < timestamp)) {
                return std::unexpected{FakeSourceError::InvalidBatch};
            }
            sampleId = sample.Id.Value;
            timestamp = std::max(timestamp, sample.TimeNanoseconds);
            if (sample.Origin == SampleOrigin::Corrected) {
                if (!MatchesPreviousPrediction(next, sample)) {
                    return std::unexpected{FakeSourceError::InvalidBatch};
                }
                for (std::size_t previous{}; previous < index; ++previous) {
                    if (batch.Samples[previous].Origin == SampleOrigin::Corrected &&
                        batch.Samples[previous].Corrects == sample.Corrects) {
                        return std::unexpected{FakeSourceError::InvalidBatch};
                    }
                }
                committedSequence = std::max(committedSequence, sample.Sequence);
            } else if (sample.Origin == SampleOrigin::Predicted) {
                if (sample.Sequence <= committedSequence ||
                    (predictionCount > 0U && sample.Sequence <= predictionSequences[predictionCount - 1U])) {
                    return std::unexpected{FakeSourceError::InvalidBatch};
                }
                predictionIds[predictionCount] = sample.Id;
                predictionSequences[predictionCount] = sample.Sequence;
                ++predictionCount;
            } else {
                if (sample.Sequence <= committedSequence) {
                    return std::unexpected{FakeSourceError::InvalidBatch};
                }
                committedSequence = sample.Sequence;
            }
            lastSequenceInBatch = sample.Sequence;
        }

        next.LastBatchOrdinal = batch.BatchOrdinal;
        next.LastSampleId = sampleId;
        next.LastCommittedSequence = committedSequence;
        next.LastTimestamp = timestamp;
        next.PreviousPredictionCount = predictionCount;
        std::copy_n(predictionIds.begin(), predictionCount, next.PreviousPredictionIds.begin());
        std::copy_n(predictionSequences.begin(), predictionCount, next.PreviousPredictionSequences.begin());
        if (terminal) {
            next.ContactOpen = false;
            next.Contact = {};
            next.PreviousPredictionCount = 0U;
        }
        return next;
    }
};

FakeInputSource::FakeInputSource(std::unique_ptr<Impl> implementation) noexcept
    : _implementation{std::move(implementation)} {}

FakeInputSource::~FakeInputSource() = default;

std::expected<std::unique_ptr<FakeInputSource>, FakeSourceError>
CreateFakeInputSource(const FakeInputSourceDescriptor &descriptor) noexcept {
    if (descriptor.Capabilities.MaximumBatchSamples == 0U ||
        descriptor.Capabilities.MaximumBatchSamples > MaximumBatchSamples || descriptor.MaximumQueuedBatches == 0U ||
        descriptor.MaximumQueuedBatches > MaximumQueuedBatches ||
        (descriptor.Capabilities.Correction && !descriptor.Capabilities.Prediction)) {
        return std::unexpected{FakeSourceError::InvalidDescriptor};
    }
    auto implementation{std::unique_ptr<FakeInputSource::Impl>{new (std::nothrow) FakeInputSource::Impl{}}};
    if (!implementation) {
        return std::unexpected{FakeSourceError::OutOfMemory};
    }
    const std::size_t sampleCapacity{static_cast<std::size_t>(descriptor.MaximumQueuedBatches) *
                                     descriptor.Capabilities.MaximumBatchSamples};
    implementation->Slots.reset(new (std::nothrow) BatchSlot[descriptor.MaximumQueuedBatches]{});
    implementation->SampleStorage.reset(new (std::nothrow) InputSample[sampleCapacity]{});
    if (!implementation->Slots || !implementation->SampleStorage) {
        return std::unexpected{FakeSourceError::OutOfMemory};
    }
    implementation->Capabilities = descriptor.Capabilities;
    implementation->QueueCapacity = descriptor.MaximumQueuedBatches;
    for (std::uint32_t index{}; index < implementation->QueueCapacity; ++index) {
        implementation->Slots[index].Samples =
            implementation->SampleStorage.get() +
            static_cast<std::size_t>(index) * implementation->Capabilities.MaximumBatchSamples;
    }
    auto source{std::unique_ptr<FakeInputSource>{new (std::nothrow) FakeInputSource{std::move(implementation)}}};
    if (!source) {
        return std::unexpected{FakeSourceError::OutOfMemory};
    }
    return source;
}

InputSourceCapabilities FakeInputSource::Capabilities() const noexcept { return _implementation->Capabilities; }

std::expected<void, InputSourceError> FakeInputSource::Start(IInputReceiver &receiver) {
    if (_implementation->Receiver != nullptr) {
        return std::unexpected{InputSourceError::AlreadyStarted};
    }
    _implementation->Receiver = &receiver;
    return {};
}

void FakeInputSource::Stop() noexcept { _implementation->ResetDelivery(); }

std::expected<void, FakeSourceError> FakeInputSource::Enqueue(const InputBatch &batch) noexcept {
    if (_implementation->QueueSize == _implementation->QueueCapacity) {
        return std::unexpected{FakeSourceError::QueueFull};
    }
    const auto nextState{_implementation->Validate(batch)};
    if (!nextState) {
        return std::unexpected{nextState.error()};
    }
    const std::uint32_t tail{(_implementation->QueueHead + _implementation->QueueSize) %
                             _implementation->QueueCapacity};
    BatchSlot &slot{_implementation->Slots[tail]};
    std::copy(batch.Samples.begin(), batch.Samples.end(), slot.Samples);
    slot.Contact = batch.Contact;
    slot.BatchOrdinal = batch.BatchOrdinal;
    slot.Phase = batch.Phase;
    slot.SampleCount = static_cast<std::uint32_t>(batch.Samples.size());
    _implementation->Script = *nextState;
    ++_implementation->QueueSize;
    return {};
}

std::expected<void, DispatchError> FakeInputSource::DispatchNext() noexcept {
    if (_implementation->Receiver == nullptr) {
        return std::unexpected{DispatchError::NotStarted};
    }
    if (_implementation->FailNext) {
        IInputReceiver *receiver{_implementation->Receiver};
        _implementation->ResetDelivery();
        receiver->SourceFailed(InputSourceError::BackendFailure);
        return std::unexpected{DispatchError::BackendFailure};
    }
    if (_implementation->QueueSize == 0U) {
        return std::unexpected{DispatchError::QueueEmpty};
    }
    BatchSlot &slot{_implementation->Slots[_implementation->QueueHead]};
    const InputBatch batch{.Contact = slot.Contact,
                           .BatchOrdinal = slot.BatchOrdinal,
                           .Phase = slot.Phase,
                           .Samples = {slot.Samples, slot.SampleCount}};
    switch (_implementation->Receiver->Receive(batch)) {
    case DeliveryResult::Accepted:
        _implementation->QueueHead = (_implementation->QueueHead + 1U) % _implementation->QueueCapacity;
        --_implementation->QueueSize;
        return {};
    case DeliveryResult::Busy:
        return std::unexpected{DispatchError::ReceiverBusy};
    case DeliveryResult::Rejected:
        _implementation->QueueHead = 0U;
        _implementation->QueueSize = 0U;
        _implementation->Script = {};
        return std::unexpected{DispatchError::ReceiverRejected};
    }
    return std::unexpected{DispatchError::ReceiverRejected};
}

void FakeInputSource::FailNext() noexcept { _implementation->FailNext = true; }

std::uint32_t FakeInputSource::QueuedBatchCount() const noexcept { return _implementation->QueueSize; }
} // namespace lightphi::input::fake
