/**
 * {file}
 * {brief} Declares the private bounded Apple event delivery state machine.
 *
 * Platform frameworks and public LightPHI types stay outside this file.
 */
#pragma once

#include "apple_event_sink.h"

#include <cstdint>
#include <gsl/pointers>
#include <memory>

namespace lightphi::input::apple_detail
{
    /** {brief} Contact phase produced for the LightPHI adapter. */
    enum class DeliveryPhase : std::uint8_t
    {
        Begin,  ///< Opens a contact.
        Update, ///< Extends a contact.
        End,    ///< Closes a contact.
        Cancel  ///< Abandons a contact.
    };

    /** {brief} One queued delivery in framework-free values. */
    struct Delivery
    {
        std::uint32_t Slot{};            ///< Stable queue slot used across a busy retry.
        std::uint64_t Contact{};         ///< Source-local contact identity.
        std::uint64_t BatchOrdinal{};    ///< Contact-local delivery ordinal.
        std::uint64_t Sample{};          ///< Source-local sample identity, or zero for cancellation.
        std::uint64_t Sequence{};        ///< Contact-local sample sequence.
        std::uint64_t TimeNanoseconds{}; ///< Monotonic event time.
        double        X{};               ///< Horizontal window position.
        double        Y{};               ///< Vertical window position.
        double        Pressure{};        ///< Normalized force.
        double        TiltXRadians{};    ///< Horizontal tilt in radians.
        double        TiltYRadians{};    ///< Vertical tilt in radians.
        double        TwistRadians{};    ///< Axial rotation in radians.
        DeliveryPhase Phase{};           ///< Contact lifecycle position.
        bool          Eraser{};          ///< Whether the eraser end is active.
    };

    /** {brief} Decision returned by the module adapter for one borrowed delivery. */
    enum class DeliveryDecision : std::uint8_t
    {
        Accepted, ///< The receiver consumed the delivery.
        Busy,     ///< The same queue slot remains for retry.
        Rejected  ///< The active contact and queued events are abandoned.
    };

    /** {brief} Why Apple delivery stopped unexpectedly. */
    enum class DeliveryFailure : std::uint8_t
    {
        Overflow ///< The fixed queue could not retain another captured event.
    };

    /** {brief} Receives bounded deliveries from the private delivery core. */
    class IDeliveryTarget
    {
      public:
        IDeliveryTarget()                                   = default;
        IDeliveryTarget(const IDeliveryTarget &)            = delete;
        IDeliveryTarget(IDeliveryTarget &&)                 = delete;
        IDeliveryTarget &operator=(const IDeliveryTarget &) = delete;
        IDeliveryTarget &operator=(IDeliveryTarget &&)      = delete;
        virtual ~IDeliveryTarget()                          = default;

        /** {brief} Consume one delivery borrowed for the duration of the call. */
        [[nodiscard]] virtual DeliveryDecision Receive(const Delivery &delivery) noexcept = 0;
        /** {brief} Observe a terminal failure after which the core is stopped. */
        virtual void Failed(DeliveryFailure failure) noexcept = 0;
    };

    /** {brief} Fixed queue and contact state shared by Apple capture adapters. */
    class AppleDeliveryCore final : public IEventTarget
    {
      public:
        AppleDeliveryCore(std::uint32_t queueCapacity, gsl::not_null<IDeliveryTarget *> target) noexcept;
        ~AppleDeliveryCore() override;

        AppleDeliveryCore(const AppleDeliveryCore &)            = delete;
        AppleDeliveryCore(AppleDeliveryCore &&)                 = delete;
        AppleDeliveryCore &operator=(const AppleDeliveryCore &) = delete;
        AppleDeliveryCore &operator=(AppleDeliveryCore &&)      = delete;

        /** {brief} Whether construction allocated the requested fixed queue. */
        [[nodiscard]] bool IsValid() const noexcept;
        /** {brief} Begin a receiver session with empty contact state. */
        void Start() noexcept;
        /** {brief} Cancel a contact once, discard queued input, and end the session. */
        void Stop() noexcept;
        /** {brief} Translate, queue, and synchronously drain one captured event. */
        void Submit(const CapturedEvent &event) noexcept override;
        /** {brief} Capabilities observed from the current AppKit device. */
        [[nodiscard]] std::uint16_t Capabilities() const noexcept;

      private:
        [[nodiscard]] bool _enqueue(const Delivery &delivery) noexcept;
        void               _drain() noexcept;
        void               _resetContact() noexcept;
        void               _fail(DeliveryFailure failure) noexcept;

        // std::unique_ptr<T[]> is the owning form of a nothrow dynamic array. LightPHI is compiled
        // without exceptions, so std::vector cannot report allocation failure and would abort where
        // this core must degrade to an invalid, reportable state.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
        std::unique_ptr<Delivery[]>      _queue;
        gsl::not_null<IDeliveryTarget *> _target;
        std::uint32_t                    _capacity{};
        std::uint32_t                    _head{};
        std::uint32_t                    _size{};
        std::uint16_t                    _capabilities{};
        std::uint64_t                    _nextContact{};
        std::uint64_t                    _nextSample{};
        std::uint64_t                    _contact{};
        std::uint64_t                    _batchOrdinal{};
        std::uint64_t                    _sequence{};
        bool                             _started{};
        bool                             _contactOpen{};
    };
} // namespace lightphi::input::apple_detail
