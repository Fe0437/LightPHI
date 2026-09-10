/**
 * {file}
 * {brief} Implements bounded framework-free Apple event delivery.
 *
 * No AppKit, UIKit, receiver, or public module type is used here.
 */
#include "apple_delivery_core.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <numbers>

namespace lightphi::input::apple_detail
{
    namespace
    {
        constexpr double HalfPi{std::numbers::pi_v<double> / 2.0};

        [[nodiscard]] constexpr std::uint16_t Bit(CapturedCapability capability) noexcept
        {
            return static_cast<std::uint16_t>(capability);
        }
    } // namespace

    AppleDeliveryCore::AppleDeliveryCore(const std::uint32_t                    queueCapacity,
                                         const gsl::not_null<IDeliveryTarget *> target) noexcept
        : _queue{new (std::nothrow) Delivery[queueCapacity]{}}, _target{target}, _capacity{queueCapacity},
          _capabilities{Bit(CapturedCapability::Timestamp)}
    {
    }

    AppleDeliveryCore::~AppleDeliveryCore() = default;

    bool AppleDeliveryCore::IsValid() const noexcept
    {
        // The delivery target is gsl::not_null, so only the fixed queue allocation can fail here.
        return _queue != nullptr && _capacity != 0U;
    }

    void AppleDeliveryCore::Start() noexcept
    {
        _head    = 0U;
        _size    = 0U;
        _started = true;
        _resetContact();
    }

    void AppleDeliveryCore::Stop() noexcept
    {
        if (_started && _contactOpen)
        {
            _head = 0U;
            _size = 0U;
            const Delivery cancellation{
                .Slot = 0U, .Contact = _contact, .BatchOrdinal = _batchOrdinal + 1U, .Phase = DeliveryPhase::Cancel};
            _queue[0] = cancellation;
            _size     = 1U;
            _drain();
        }
        _head    = 0U;
        _size    = 0U;
        _started = false;
        _resetContact();
    }

    void AppleDeliveryCore::Submit(const CapturedEvent &event) noexcept
    {
        if (!_started)
        {
            return;
        }
        if (event.Kind == CapturedEventKind::DeviceObserved)
        {
            _capabilities = event.Capabilities | Bit(CapturedCapability::Timestamp);
            return;
        }
        if (event.Kind == CapturedEventKind::Cancel)
        {
            if (_contactOpen)
            {
                if (!_enqueue({.Contact = _contact, .BatchOrdinal = ++_batchOrdinal, .Phase = DeliveryPhase::Cancel}))
                {
                    return;
                }
                _resetContact();
                _drain();
            }
            return;
        }
        if (event.Kind == CapturedEventKind::Begin && _contactOpen)
        {
            if (!_enqueue({.Contact = _contact, .BatchOrdinal = ++_batchOrdinal, .Phase = DeliveryPhase::Cancel}))
            {
                return;
            }
            _resetContact();
        }
        if (event.Kind == CapturedEventKind::Begin)
        {
            _contactOpen  = true;
            _contact      = ++_nextContact;
            _batchOrdinal = 0U;
            _sequence     = 0U;
        }
        else if (!_contactOpen)
        {
            return;
        }

        if (event.Pressure != 0.0)
        {
            _capabilities |= Bit(CapturedCapability::Pressure);
        }
        if (event.TiltXRadians != 0.0 || event.TiltYRadians != 0.0)
        {
            _capabilities |= Bit(CapturedCapability::Tilt);
        }
        if (event.TwistRadians != 0.0)
        {
            _capabilities |= Bit(CapturedCapability::Twist);
        }

        DeliveryPhase phase{DeliveryPhase::Update};
        if (event.Kind == CapturedEventKind::Begin)
        {
            phase = DeliveryPhase::Begin;
        }
        else if (event.Kind == CapturedEventKind::End)
        {
            phase = DeliveryPhase::End;
        }
        if (!_enqueue({.Contact         = _contact,
                       .BatchOrdinal    = ++_batchOrdinal,
                       .Sample          = ++_nextSample,
                       .Sequence        = ++_sequence,
                       .TimeNanoseconds = event.TimeNanoseconds,
                       .X               = event.X,
                       .Y               = event.Y,
                       .Pressure        = std::clamp(event.Pressure, 0.0, 1.0),
                       .TiltXRadians    = std::clamp(event.TiltXRadians, -HalfPi, HalfPi),
                       .TiltYRadians    = std::clamp(event.TiltYRadians, -HalfPi, HalfPi),
                       .TwistRadians    = event.TwistRadians,
                       .Phase           = phase,
                       .Eraser          = event.Eraser}))
        {
            return;
        }
        if (event.Kind == CapturedEventKind::End)
        {
            _resetContact();
        }
        _drain();
    }

    std::uint16_t AppleDeliveryCore::Capabilities() const noexcept
    {
        return _capabilities;
    }

    bool AppleDeliveryCore::_enqueue(const Delivery &delivery) noexcept
    {
        if (_size == _capacity)
        {
            _fail(DeliveryFailure::Overflow);
            return false;
        }
        const std::uint32_t tail{(_head + _size) % _capacity};
        _queue[tail]      = delivery;
        _queue[tail].Slot = tail;
        ++_size;
        return true;
    }

    void AppleDeliveryCore::_drain() noexcept
    {
        while (_started && _size != 0U)
        {
            switch (_target->Receive(_queue[_head]))
            {
                case DeliveryDecision::Accepted:
                    _head = (_head + 1U) % _capacity;
                    --_size;
                    break;
                case DeliveryDecision::Busy:
                    return;
                case DeliveryDecision::Rejected:
                    _head = 0U;
                    _size = 0U;
                    _resetContact();
                    return;
            }
        }
    }

    void AppleDeliveryCore::_resetContact() noexcept
    {
        _contactOpen  = false;
        _contact      = 0U;
        _batchOrdinal = 0U;
        _sequence     = 0U;
    }

    void AppleDeliveryCore::_fail(const DeliveryFailure failure) noexcept
    {
        _head    = 0U;
        _size    = 0U;
        _started = false;
        _resetContact();
        _target->Failed(failure);
    }
} // namespace lightphi::input::apple_detail
