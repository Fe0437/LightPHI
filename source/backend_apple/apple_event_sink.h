/**
 * {file}
 * {brief} Defines the private value boundary between Swift capture and C++ delivery.
 *
 * AppKit and UIKit objects never cross this boundary.
 */
#pragma once

#include <cstdint>
#include <gsl/pointers>

namespace lightphi::input::apple_detail
{
    /** {brief} Apple event kind understood by the shared delivery core. */
    enum class CapturedEventKind : std::uint8_t
    {
        DeviceObserved, ///< Updates the selected device capability snapshot.
        Begin,          ///< Starts contact with one measured point.
        Update,         ///< Extends the active contact with one measured point.
        End,            ///< Ends the active contact with one measured point.
        Cancel          ///< Cancels the active contact without a point.
    };

    /** {brief} Backend-neutral capability bits shared with the LightPHI input contract. */
    enum class CapturedCapability : std::uint16_t
    {
        None       = 0U,       ///< No optional capability.
        Pressure   = 1U << 0U, ///< Physical force is available.
        Coalescing = 1U << 1U, ///< Earlier observations are available.
        Prediction = 1U << 2U, ///< Provisional observations are available.
        Correction = 1U << 3U, ///< Prediction replacements are available.
        Timestamp  = 1U << 4U, ///< Monotonic event time is available.
        Tilt       = 1U << 5U, ///< Two-axis tilt is available.
        Twist      = 1U << 6U, ///< Axial rotation is available.
        Eraser     = 1U << 7U, ///< The eraser end is distinguishable.
        Hover      = 1U << 8U  ///< Hover positions are available.
    };

    /** {brief} One captured Apple event expressed without framework objects. */
    struct CapturedEvent
    {
        CapturedEventKind Kind{};            ///< Device or contact event kind.
        std::uint16_t     Capabilities{};    ///< Capabilities observed at capture time.
        std::uint64_t     TimeNanoseconds{}; ///< Monotonic event time.
        double            X{};               ///< Horizontal window position.
        double            Y{};               ///< Vertical window position.
        double            Pressure{};        ///< Normalized physical force.
        double            TiltXRadians{};    ///< Horizontal tilt in radians.
        double            TiltYRadians{};    ///< Vertical tilt in radians.
        double            TwistRadians{};    ///< Axial rotation in radians.
        bool              Eraser{};          ///< Whether the eraser end is active.
    };

    /** {brief} Consumes framework-free captured events on the capture thread. */
    class IEventTarget
    {
      public:
        IEventTarget()                                = default;
        IEventTarget(const IEventTarget &)            = delete;
        IEventTarget(IEventTarget &&)                 = delete;
        IEventTarget &operator=(const IEventTarget &) = delete;
        IEventTarget &operator=(IEventTarget &&)      = delete;
        virtual ~IEventTarget()                       = default;

        /** {brief} Translate, queue, and drain one captured event; true while deliveries wait for a retry. */
        [[nodiscard]] virtual bool Submit(const CapturedEvent &event) noexcept = 0;
        /** {brief} Offer waiting deliveries again; true while some still wait. */
        [[nodiscard]] virtual bool RetryPending() noexcept = 0;
    };

    /**
     * {brief} Receives framework-free values from a platform capture adapter.
     *
     * Swift holds this by raw pointer and calls its non-virtual methods directly, so the type stays
     * concrete and non-polymorphic. The dispatch to IEventTarget happens entirely inside C++.
     */
    class AppleEventSink
    {
      public:
        explicit constexpr AppleEventSink(gsl::not_null<IEventTarget *> target) noexcept : _target{target} {}

        /** {brief} Record capabilities observed by one Apple platform adapter. */
        void ObserveDevice(std::uint16_t capabilities) const noexcept
        {
            static_cast<void>(_submitEvent({.Kind = CapturedEventKind::DeviceObserved, .Capabilities = capabilities}));
        }

        /**
         * {brief} Submit one contact event already translated by an Apple adapter.
         *
         * Returns true while deliveries wait for a busy receiver. Nothing else offers them again if the
         * pen goes quiet, so the adapter then schedules RetryPending.
         */
        [[nodiscard]] bool Submit(const CapturedEvent &event) const noexcept
        {
            return _submitEvent(event);
        }

        /** {brief} Cancel the active captured contact; true while deliveries wait for a retry. */
        [[nodiscard]] bool Cancel() const noexcept
        {
            return _submitEvent({.Kind = CapturedEventKind::Cancel});
        }

        /** {brief} Offer deliveries a busy receiver refused again; true while some still wait. */
        [[nodiscard]] bool RetryPending() const noexcept
        {
            return _target->RetryPending();
        }

      private:
        [[nodiscard]] bool _submitEvent(const CapturedEvent &event) const noexcept
        {
            return _target->Submit(event);
        }

        gsl::not_null<IEventTarget *> _target;
    };
} // namespace lightphi::input::apple_detail
