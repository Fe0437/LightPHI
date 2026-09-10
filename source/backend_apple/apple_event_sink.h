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

        /** {brief} Translate, queue, and drain one captured event. */
        virtual void Submit(const CapturedEvent &event) noexcept = 0;
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
            _submitEvent({.Kind = CapturedEventKind::DeviceObserved, .Capabilities = capabilities});
        }

        /** {brief} Submit one measured point already normalized by an Apple adapter. */
        void SubmitMeasuredPoint(std::uint8_t kind, std::uint64_t timestamp, double x, double y, double pressure,
                                 double tiltXRadians, double tiltYRadians, double twistRadians,
                                 bool eraser) const noexcept
        {
            _submitEvent({.Kind            = static_cast<CapturedEventKind>(kind),
                          .TimeNanoseconds = timestamp,
                          .X               = x,
                          .Y               = y,
                          .Pressure        = pressure,
                          .TiltXRadians    = tiltXRadians,
                          .TiltYRadians    = tiltYRadians,
                          .TwistRadians    = twistRadians,
                          .Eraser          = eraser});
        }

        /** {brief} Cancel the active captured contact. */
        void Cancel() const noexcept
        {
            _submitEvent({.Kind = CapturedEventKind::Cancel});
        }

      private:
        void _submitEvent(const CapturedEvent &event) const noexcept
        {
            _target->Submit(event);
        }

        gsl::not_null<IEventTarget *> _target;
    };
} // namespace lightphi::input::apple_detail
