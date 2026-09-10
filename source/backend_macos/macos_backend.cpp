/**
 * {file}
 * {brief} Adapts Swift AppKit capture to the LightPHI input source contract.
 *
 * Framework objects and Swift ownership never cross the exported module API.
 */
module;

#include "../backend_apple/apple_delivery_core.h"
#include "LightPHIMacOSCapture-Swift.h"

#include <cstdint>
#include <expected>
#include <gsl/pointers>
#include <memory>
#include <new>
#include <span>

module lightphi.backend;

namespace lightphi::input
{
    namespace
    {
        using apple_detail::CapturedCapability;
        using apple_detail::Delivery;
        using apple_detail::DeliveryDecision;
        using apple_detail::DeliveryFailure;
        using apple_detail::DeliveryPhase;

        static_assert(static_cast<std::uint16_t>(InputCapability::Pressure) ==
                      static_cast<std::uint16_t>(CapturedCapability::Pressure));
        static_assert(static_cast<std::uint16_t>(InputCapability::Timestamp) ==
                      static_cast<std::uint16_t>(CapturedCapability::Timestamp));
        static_assert(static_cast<std::uint16_t>(InputCapability::Tilt) ==
                      static_cast<std::uint16_t>(CapturedCapability::Tilt));
        static_assert(static_cast<std::uint16_t>(InputCapability::Twist) ==
                      static_cast<std::uint16_t>(CapturedCapability::Twist));
        static_assert(static_cast<std::uint16_t>(InputCapability::Eraser) ==
                      static_cast<std::uint16_t>(CapturedCapability::Eraser));

        [[nodiscard]] ContactPhase ToContactPhase(const DeliveryPhase phase) noexcept
        {
            switch (phase)
            {
                case DeliveryPhase::Begin:
                    return ContactPhase::Begin;
                case DeliveryPhase::Update:
                    return ContactPhase::Update;
                case DeliveryPhase::End:
                    return ContactPhase::End;
                case DeliveryPhase::Cancel:
                    return ContactPhase::Cancel;
            }
            return ContactPhase::Cancel;
        }
    } // namespace

    class InputSourceImplementation final : public apple_detail::IDeliveryTarget
    {
      public:
        // Core is declared before Sink, so it is constructed before Sink borrows it. Passing this to
        // Core is safe because only the IDeliveryTarget base subobject is used, and bases are
        // initialized first.
        explicit InputSourceImplementation(const std::uint32_t queueCapacity) noexcept
            : Core{queueCapacity, gsl::not_null<apple_detail::IDeliveryTarget *>{this}},
              Sink{gsl::not_null<apple_detail::IEventTarget *>{&Core}},
              Samples{new (std::nothrow) InputSample[queueCapacity]{}}
        {
        }

        ~InputSourceImplementation() override
        {
            StopCapture();
        }

        // Core and Sink retain pointers to this object, so it is pinned.
        InputSourceImplementation(const InputSourceImplementation &)            = delete;
        InputSourceImplementation(InputSourceImplementation &&)                 = delete;
        InputSourceImplementation &operator=(const InputSourceImplementation &) = delete;
        InputSourceImplementation &operator=(InputSourceImplementation &&)      = delete;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Core.IsValid() && Samples != nullptr;
        }

        // Writes the delivered sample into the arena below, so this is not a const operation.
        [[nodiscard]] DeliveryDecision Receive(const Delivery &delivery) noexcept override
        {
            if (Receiver == nullptr)
            {
                return DeliveryDecision::Rejected;
            }
            std::span<const InputSample> samples{};
            if (delivery.Sample != 0U)
            {
                InputSample &sample{Samples[delivery.Slot]};
                if (sample.Id.Value != delivery.Sample)
                {
                    sample = {.Id              = {.Value = delivery.Sample},
                              .Sequence        = delivery.Sequence,
                              .TimeNanoseconds = delivery.TimeNanoseconds,
                              .X               = delivery.X,
                              .Y               = delivery.Y,
                              .Pressure        = delivery.Pressure,
                              .TiltXRadians    = delivery.TiltXRadians,
                              .TiltYRadians    = delivery.TiltYRadians,
                              .TwistRadians    = delivery.TwistRadians,
                              .Origin          = SampleOrigin::Measured,
                              .Eraser          = delivery.Eraser,
                              .Hovering        = false};
                }
                samples = {&sample, 1U};
            }
            const InputBatch batch{.Contact      = {.Value = delivery.Contact},
                                   .BatchOrdinal = delivery.BatchOrdinal,
                                   .Phase        = ToContactPhase(delivery.Phase),
                                   .Samples      = samples};
            switch (Receiver->Receive(batch))
            {
                case DeliveryResult::Accepted:
                    return DeliveryDecision::Accepted;
                case DeliveryResult::Busy:
                    return DeliveryDecision::Busy;
                case DeliveryResult::Rejected:
                    return DeliveryDecision::Rejected;
            }
            return DeliveryDecision::Rejected;
        }

        void Failed(DeliveryFailure /*failure*/) noexcept override
        {
            IInputReceiver *receiver{Receiver};
            Receiver = nullptr;
            StopCapture();
            if (receiver != nullptr)
            {
                gsl::not_null<IInputReceiver *>{receiver}->SourceFailed(InputSourceError::Overflow);
            }
        }

        [[nodiscard]] bool StartCapture() noexcept
        {
            if (Monitor != nullptr)
            {
                LightPHIMacOSCapture::destroyAppleInputMonitor(Monitor);
            }
            Monitor = LightPHIMacOSCapture::createAppleInputMonitor(&Sink);
            return Monitor != nullptr && LightPHIMacOSCapture::startAppleInputMonitor(Monitor);
        }

        void StopCapture() noexcept
        {
            if (Monitor != nullptr)
            {
                LightPHIMacOSCapture::stopAppleInputMonitor(Monitor);
                LightPHIMacOSCapture::destroyAppleInputMonitor(Monitor);
                Monitor = nullptr;
            }
        }

        apple_detail::AppleDeliveryCore Core;
        apple_detail::AppleEventSink    Sink;
        // std::unique_ptr<T[]> is the owning form of a nothrow dynamic array. LightPHI is compiled
        // without exceptions, so std::vector cannot report allocation failure and would abort where
        // this backend must return an InputSourceCreationError.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
        std::unique_ptr<InputSample[]> Samples;
        IInputReceiver                *Receiver{};
        // Opaque Swift handle. createAppleInputMonitor returns Unmanaged<...>.toOpaque(), which the
        // generated header types as void*, so this one erased pointer is imposed by Swift interop
        // rather than chosen here.
        void *Monitor{};
    };

    void InputSource::ImplementationDeleter::operator()(InputSourceImplementation *implementation) const noexcept
    {
        const std::unique_ptr<InputSourceImplementation> owned{implementation};
    }

    InputSource::InputSource(Implementation implementation) noexcept : _implementation{std::move(implementation)} {}

    InputSource::~InputSource()
    {
        Stop();
    }

    std::expected<std::unique_ptr<InputSource>, InputSourceCreationError>
    CreateInputSource(const InputSourceDescriptor &descriptor) noexcept
    {
        if (descriptor.MaximumQueuedBatches == 0U || descriptor.MaximumQueuedBatches > MaximumBackendQueuedBatches)
        {
            return std::unexpected{InputSourceCreationError::InvalidDescriptor};
        }
        InputSource::Implementation implementation{new (std::nothrow)
                                                       InputSourceImplementation{descriptor.MaximumQueuedBatches}};
        if (!implementation || !implementation->IsValid())
        {
            return std::unexpected{InputSourceCreationError::OutOfMemory};
        }
        auto source{std::unique_ptr<InputSource>{new (std::nothrow) InputSource{std::move(implementation)}}};
        if (!source)
        {
            return std::unexpected{InputSourceCreationError::OutOfMemory};
        }
        return source;
    }

    InputSourceCapabilities InputSource::Capabilities() const noexcept
    {
        return {.Available           = static_cast<InputCapability>(_implementation->Core.Capabilities()),
                .MaximumBatchSamples = 1U};
    }

    std::expected<void, InputSourceError> InputSource::Start(IInputReceiver &receiver)
    {
        auto &state{*_implementation};
        if (state.Receiver != nullptr)
        {
            return std::unexpected{InputSourceError::AlreadyStarted};
        }
        state.Receiver = &receiver;
        state.Core.Start();
        if (!state.StartCapture())
        {
            state.Core.Stop();
            state.Receiver = nullptr;
            state.StopCapture();
            return std::unexpected{InputSourceError::Unavailable};
        }
        return {};
    }

    void InputSource::Stop() noexcept
    {
        auto &state{*_implementation};
        state.StopCapture();
        state.Core.Stop();
        state.Receiver = nullptr;
    }
} // namespace lightphi::input
