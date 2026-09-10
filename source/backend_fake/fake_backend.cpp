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
#include <gsl/pointers>
#include <memory>
#include <new>
#include <optional>
#include <span>

module lightphi.backend.fake;

namespace lightphi::input::fake
{
    namespace
    {
        struct BatchSlot
        {
            ContactId     Contact{};
            std::uint64_t BatchOrdinal{};
            ContactPhase  Phase{};
            std::size_t   SampleOffset{};
            std::uint32_t SampleCount{};
        };
    } // namespace

    class FakeInputSourceImplementation
    {
      public:
        InputSourceCapabilities Capabilities{};
        std::uint32_t           QueueCapacity{};
        std::uint32_t           QueueHead{};
        std::uint32_t           QueueSize{};
        // std::unique_ptr<T[]> is the owning form of a nothrow dynamic array. LightPHI is compiled
        // without exceptions, so std::vector cannot report allocation failure and would abort where
        // this backend must return FakeSourceError::OutOfMemory.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
        std::unique_ptr<BatchSlot[]> Slots{};
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
        std::unique_ptr<InputSample[]> SampleStorage{};
        std::size_t                    SampleCapacity{};
        IInputReceiver                *Receiver{};
        bool                           FailNext{};
        DeliveryState                  Script{};
        // The contact the receiver has seen open, which is what Stop must cancel. The script's own
        // contact can run ahead of it by every batch still queued.
        ContactId     DeliveredContact{};
        std::uint64_t DeliveredBatchOrdinal{};

        void ResetDelivery() noexcept
        {
            QueueHead        = 0U;
            QueueSize        = 0U;
            Receiver         = nullptr;
            FailNext         = false;
            Script           = {};
            DeliveredContact = {};
        }

        void RecordDelivered(const BatchSlot &slot) noexcept
        {
            const bool closes{slot.Phase == ContactPhase::End || slot.Phase == ContactPhase::Cancel};
            DeliveredContact      = closes ? ContactId{} : slot.Contact;
            DeliveredBatchOrdinal = slot.BatchOrdinal;
        }
    };

    namespace
    {
        /** {brief} The fake's own name for a delivery no device could have produced. */
        [[nodiscard]] FakeSourceError ScriptError(const BatchValidationError &error) noexcept
        {
            if (error.Reason == BatchValidationReason::TooManySamples)
            {
                return FakeSourceError::BatchTooLarge;
            }
            if (error.Reason == BatchValidationReason::InvalidSample &&
                error.Sample.Reason == SampleValidationReason::MissingCapability)
            {
                return FakeSourceError::UndeclaredValue;
            }
            return FakeSourceError::InvalidBatch;
        }
    } // namespace

    void
    FakeInputSource::ImplementationDeleter::operator()(FakeInputSourceImplementation *implementation) const noexcept
    {
        const std::unique_ptr<FakeInputSourceImplementation> owned{implementation};
    }

    FakeInputSource::FakeInputSource(Implementation implementation) noexcept
        : _implementation{std::move(implementation)}
    {
    }

    FakeInputSource::~FakeInputSource() = default;

    std::expected<std::unique_ptr<FakeInputSource>, FakeSourceError>
    FakeInputSource::Create(const FakeInputSourceDescriptor &descriptor) noexcept
    {
        if (descriptor.Capabilities.MaximumBatchSamples == 0U ||
            descriptor.Capabilities.MaximumBatchSamples > MaximumBatchSamples ||
            descriptor.MaximumQueuedBatches == 0U || descriptor.MaximumQueuedBatches > MaximumQueuedBatches ||
            (static_cast<unsigned>(descriptor.Capabilities.Available) & ~static_cast<unsigned>(AllInputCapabilities)) !=
                0U ||
            (Supports(descriptor.Capabilities, InputCapability::Correction) &&
             !Supports(descriptor.Capabilities, InputCapability::Prediction)))
        {
            return std::unexpected{FakeSourceError::InvalidDescriptor};
        }
        Implementation implementation{new (std::nothrow) FakeInputSourceImplementation{}};
        if (!implementation)
        {
            return std::unexpected{FakeSourceError::OutOfMemory};
        }
        auto             &state{*implementation};
        const std::size_t sampleCapacity{static_cast<std::size_t>(descriptor.MaximumQueuedBatches) *
                                         descriptor.Capabilities.MaximumBatchSamples};
        state.Slots         = decltype(state.Slots){new (std::nothrow) BatchSlot[descriptor.MaximumQueuedBatches]{}};
        state.SampleStorage = decltype(state.SampleStorage){new (std::nothrow) InputSample[sampleCapacity]{}};
        if (!state.Slots || !state.SampleStorage)
        {
            return std::unexpected{FakeSourceError::OutOfMemory};
        }
        state.SampleCapacity = sampleCapacity;
        state.Capabilities   = descriptor.Capabilities;
        state.QueueCapacity  = descriptor.MaximumQueuedBatches;
        for (std::uint32_t index{}; index < state.QueueCapacity; ++index)
        {
            state.Slots[index].SampleOffset = static_cast<std::size_t>(index) * state.Capabilities.MaximumBatchSamples;
        }
        auto source{std::unique_ptr<FakeInputSource>{new (std::nothrow) FakeInputSource{std::move(implementation)}}};
        if (!source)
        {
            return std::unexpected{FakeSourceError::OutOfMemory};
        }
        return source;
    }

    InputSourceCapabilities FakeInputSource::Capabilities() const noexcept
    {
        return _implementation->Capabilities;
    }

    std::expected<void, InputSourceError> FakeInputSource::Start(IInputReceiver &receiver)
    {
        auto &state{*_implementation};
        if (state.Receiver != nullptr)
        {
            return std::unexpected{InputSourceError::AlreadyStarted};
        }
        state.Receiver = &receiver;
        return {};
    }

    void FakeInputSource::Stop() noexcept
    {
        auto &state{*_implementation};
        if (state.Receiver != nullptr && state.DeliveredContact.Value != 0U)
        {
            // Stopping cannot wait, so the cancellation is offered once whatever the receiver answers.
            static_cast<void>(state.Receiver->Receive({.Contact      = state.DeliveredContact,
                                                       .BatchOrdinal = state.DeliveredBatchOrdinal + 1U,
                                                       .Phase        = ContactPhase::Cancel}));
        }
        state.ResetDelivery();
    }

    std::expected<void, FakeSourceError> FakeInputSource::Enqueue(const InputBatch &batch) noexcept
    {
        auto &state{*_implementation};
        if (state.QueueSize == state.QueueCapacity)
        {
            return std::unexpected{FakeSourceError::QueueFull};
        }
        const auto nextState{ValidateBatch(batch, state.Capabilities, state.Script)};
        if (!nextState)
        {
            return std::unexpected{ScriptError(nextState.error())};
        }
        const std::uint32_t tail{(state.QueueHead + state.QueueSize) % state.QueueCapacity};
        BatchSlot          &slot{state.Slots[tail]};
        const std::span     sampleStorage{state.SampleStorage.get(), state.SampleCapacity};
        std::ranges::copy(batch.Samples, sampleStorage.subspan(slot.SampleOffset, batch.Samples.size()).begin());
        slot.Contact      = batch.Contact;
        slot.BatchOrdinal = batch.BatchOrdinal;
        slot.Phase        = batch.Phase;
        slot.SampleCount  = static_cast<std::uint32_t>(batch.Samples.size());
        state.Script      = *nextState;
        ++state.QueueSize;
        return {};
    }

    std::expected<void, DispatchError> FakeInputSource::DispatchNext() noexcept
    {
        auto &state{*_implementation};
        if (state.Receiver == nullptr)
        {
            return std::unexpected{DispatchError::NotStarted};
        }
        if (state.FailNext)
        {
            const gsl::not_null<IInputReceiver *> receiver{state.Receiver};
            state.ResetDelivery();
            receiver->SourceFailed(InputSourceError::BackendFailure);
            return std::unexpected{DispatchError::BackendFailure};
        }
        if (state.QueueSize == 0U)
        {
            return std::unexpected{DispatchError::QueueEmpty};
        }
        const BatchSlot &slot{state.Slots[state.QueueHead]};
        const std::span  sampleStorage{state.SampleStorage.get(), state.SampleCapacity};
        const InputBatch batch{.Contact      = slot.Contact,
                               .BatchOrdinal = slot.BatchOrdinal,
                               .Phase        = slot.Phase,
                               .Samples      = sampleStorage.subspan(slot.SampleOffset, slot.SampleCount)};
        switch (state.Receiver->Receive(batch))
        {
            case DeliveryResult::Accepted:
                state.RecordDelivered(slot);
                state.QueueHead = (state.QueueHead + 1U) % state.QueueCapacity;
                --state.QueueSize;
                return {};
            case DeliveryResult::Busy:
                return std::unexpected{DispatchError::ReceiverBusy};
            case DeliveryResult::Rejected:
                state.QueueHead        = 0U;
                state.QueueSize        = 0U;
                state.Script           = {};
                state.DeliveredContact = {};
                return std::unexpected{DispatchError::ReceiverRejected};
        }
        return std::unexpected{DispatchError::ReceiverRejected};
    }

    void FakeInputSource::FailNext() noexcept
    {
        _implementation->FailNext = true;
    }

    std::uint32_t FakeInputSource::QueuedBatchCount() const noexcept
    {
        return _implementation->QueueSize;
    }
} // namespace lightphi::input::fake
