/**
 * {file}
 * {brief} Proves the private Apple delivery core enforces its bounded contact contract.
 *
 * AppKit capture, Swift ownership, and the public LightPHI modules stay outside this test. The
 * core is framework-free, so every case here runs without an NSApplication or a real device.
 */
#include "apple_delivery_core.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace lightphi::input::apple_detail;

namespace
{
    constexpr double HalfPi{std::numbers::pi_v<double> / 2.0};

    /** {brief} Fold one or more capability bits into a mask without signed promotion. */
    template <typename... Capabilities>
    [[nodiscard]] constexpr std::uint16_t Bits(Capabilities... capabilities) noexcept
    {
        return static_cast<std::uint16_t>((0U | ... | static_cast<unsigned>(capabilities)));
    }

    /** {brief} Whether an observed capability mask carries one capability. */
    [[nodiscard]] constexpr bool Has(std::uint16_t mask, CapturedCapability capability) noexcept
    {
        return (static_cast<unsigned>(mask) & static_cast<unsigned>(capability)) != 0U;
    }

    /** {brief} Records deliveries and replays a scripted decision for each one. */
    class RecordingTarget final : public IDeliveryTarget
    {
      public:
        DeliveryDecision      Decision{DeliveryDecision::Accepted};
        std::vector<Delivery> Deliveries{};
        std::uint32_t         Failures{};
        DeliveryFailure       LastFailure{};

        [[nodiscard]] DeliveryDecision Receive(const Delivery &delivery) noexcept override
        {
            Deliveries.push_back(delivery);
            return Decision;
        }

        void Failed(const DeliveryFailure failure) noexcept override
        {
            ++Failures;
            LastFailure = failure;
        }

        [[nodiscard]] std::size_t Count() const noexcept
        {
            return Deliveries.size();
        }

        void Clear() noexcept
        {
            Deliveries.clear();
        }
    };

    [[nodiscard]] CapturedEvent Point(CapturedEventKind kind, std::uint64_t time, double x, double y) noexcept
    {
        return CapturedEvent{.Kind = kind, .TimeNanoseconds = time, .X = x, .Y = y};
    }

    /** {brief} A zero capacity queue cannot be allocated into a usable core. */
    [[nodiscard]] bool ReportsAnUnusableQueue() noexcept
    {
        RecordingTarget         target{};
        const AppleDeliveryCore empty{0U, &target};
        const AppleDeliveryCore usable{4U, &target};
        return !empty.IsValid() && usable.IsValid();
    }

    /** {brief} Capabilities start at Timestamp, are replaced by the device, and grow with values. */
    [[nodiscard]] bool ReportsObservedCapabilities() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{8U, &target};
        if (core.Capabilities() != Bits(CapturedCapability::Timestamp))
        {
            return false;
        }
        core.Start();
        core.Submit({.Kind         = CapturedEventKind::DeviceObserved,
                     .Capabilities = Bits(CapturedCapability::Pressure, CapturedCapability::Eraser)});
        if (core.Capabilities() !=
            Bits(CapturedCapability::Pressure, CapturedCapability::Eraser, CapturedCapability::Timestamp))
        {
            return false;
        }
        // A nonzero tilt or twist value adds its bit even when the device never announced it.
        CapturedEvent tilted{Point(CapturedEventKind::Begin, 10U, 1.0, 2.0)};
        tilted.TiltXRadians = 0.25;
        tilted.TwistRadians = 0.5;
        core.Submit(tilted);
        const std::uint16_t afterValues{core.Capabilities()};
        return Has(afterValues, CapturedCapability::Tilt) && Has(afterValues, CapturedCapability::Twist);
    }

    /** {brief} Events outside a session change neither deliveries nor observed capabilities. */
    [[nodiscard]] bool IgnoresEventsOutsideASession() noexcept
    {
        RecordingTarget     target{};
        AppleDeliveryCore   core{4U, &target};
        const std::uint16_t idle{core.Capabilities()};
        core.Submit(Point(CapturedEventKind::Begin, 10U, 1.0, 2.0));
        // A device report before Start must not be absorbed either, or the first Capabilities()
        // a consumer reads would describe a session that never began.
        core.Submit({.Kind = CapturedEventKind::DeviceObserved, .Capabilities = Bits(CapturedCapability::Pressure)});
        if (target.Count() != 0U || core.Capabilities() != idle)
        {
            return false;
        }
        core.Start();
        core.Stop();
        core.Submit(Point(CapturedEventKind::Begin, 20U, 1.0, 2.0));
        core.Submit({.Kind = CapturedEventKind::DeviceObserved, .Capabilities = Bits(CapturedCapability::Twist)});
        return target.Count() == 0U && core.Capabilities() == idle;
    }

    /** {brief} An update without an open contact is discarded rather than delivered. */
    [[nodiscard]] bool RequiresAnOpenContact() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{4U, &target};
        core.Start();
        core.Submit(Point(CapturedEventKind::Update, 10U, 1.0, 2.0));
        core.Submit(Point(CapturedEventKind::End, 20U, 1.0, 2.0));
        return target.Count() == 0U;
    }

    /** {brief} One contact delivers Begin, Update, and End with advancing ordinals and sequences. */
    [[nodiscard]] bool DeliversOneContactInOrder() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{8U, &target};
        core.Start();
        core.Submit(Point(CapturedEventKind::Begin, 10U, 1.0, 2.0));
        core.Submit(Point(CapturedEventKind::Update, 20U, 3.0, 4.0));
        core.Submit(Point(CapturedEventKind::End, 30U, 5.0, 6.0));
        if (target.Count() != 3U)
        {
            return false;
        }
        const Delivery &begin{target.Deliveries[0]};
        const Delivery &update{target.Deliveries[1]};
        const Delivery &end{target.Deliveries[2]};
        if (begin.Phase != DeliveryPhase::Begin || update.Phase != DeliveryPhase::Update ||
            end.Phase != DeliveryPhase::End)
        {
            return false;
        }
        // One contact identity, strictly increasing ordinals and sequences, unique sample identities.
        if (begin.Contact == 0U || update.Contact != begin.Contact || end.Contact != begin.Contact)
        {
            return false;
        }
        if (begin.BatchOrdinal != 1U || update.BatchOrdinal != 2U || end.BatchOrdinal != 3U)
        {
            return false;
        }
        if (begin.Sequence != 1U || update.Sequence != 2U || end.Sequence != 3U)
        {
            return false;
        }
        if (begin.Sample == 0U || update.Sample <= begin.Sample || end.Sample <= update.Sample)
        {
            return false;
        }
        // Positions and times pass through unchanged.
        return begin.X == 1.0 && begin.Y == 2.0 && end.TimeNanoseconds == 30U;
    }

    /** {brief} A second contact gets a new identity after the first ends. */
    [[nodiscard]] bool StartsANewContactAfterEnd() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{8U, &target};
        core.Start();
        core.Submit(Point(CapturedEventKind::Begin, 10U, 1.0, 2.0));
        core.Submit(Point(CapturedEventKind::End, 20U, 1.0, 2.0));
        const std::uint64_t first{target.Deliveries[0].Contact};
        target.Clear();
        core.Submit(Point(CapturedEventKind::Begin, 30U, 1.0, 2.0));
        return target.Count() == 1U && target.Deliveries[0].Contact != first &&
               target.Deliveries[0].BatchOrdinal == 1U && target.Deliveries[0].Sequence == 1U;
    }

    /** {brief} A Begin during an open contact cancels the old contact before opening the new one. */
    [[nodiscard]] bool CancelsTheOpenContactOnRestart() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{8U, &target};
        core.Start();
        core.Submit(Point(CapturedEventKind::Begin, 10U, 1.0, 2.0));
        const std::uint64_t first{target.Deliveries[0].Contact};
        target.Clear();
        core.Submit(Point(CapturedEventKind::Begin, 20U, 3.0, 4.0));
        if (target.Count() != 2U)
        {
            return false;
        }
        const Delivery &cancel{target.Deliveries[0]};
        const Delivery &begin{target.Deliveries[1]};
        // The cancellation closes the first contact and carries no sample.
        return cancel.Phase == DeliveryPhase::Cancel && cancel.Contact == first && cancel.Sample == 0U &&
               begin.Phase == DeliveryPhase::Begin && begin.Contact != first;
    }

    /** {brief} An explicit cancel event closes the contact without a sample. */
    [[nodiscard]] bool DeliversAnExplicitCancel() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{8U, &target};
        core.Start();
        core.Submit(Point(CapturedEventKind::Begin, 10U, 1.0, 2.0));
        target.Clear();
        core.Submit({.Kind = CapturedEventKind::Cancel});
        if (target.Count() != 1U || target.Deliveries[0].Phase != DeliveryPhase::Cancel ||
            target.Deliveries[0].Sample != 0U)
        {
            return false;
        }
        // The contact is closed, so a following update is discarded.
        target.Clear();
        core.Submit(Point(CapturedEventKind::Update, 20U, 1.0, 2.0));
        return target.Count() == 0U;
    }

    /** {brief} Stop cancels an open contact exactly once and stays quiet when none is open. */
    [[nodiscard]] bool CancelsAnOpenContactOnStop() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{8U, &target};
        core.Start();
        core.Submit(Point(CapturedEventKind::Begin, 10U, 1.0, 2.0));
        const std::uint64_t contact{target.Deliveries[0].Contact};
        target.Clear();
        core.Stop();
        if (target.Count() != 1U || target.Deliveries[0].Phase != DeliveryPhase::Cancel ||
            target.Deliveries[0].Contact != contact)
        {
            return false;
        }
        // A second Stop has no open contact left to cancel.
        target.Clear();
        core.Stop();
        return target.Count() == 0U;
    }

    /** {brief} A busy target keeps the exact delivery queued for a later retry. */
    [[nodiscard]] bool RetainsTheDeliveryWhileBusy() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{8U, &target};
        core.Start();
        target.Decision = DeliveryDecision::Busy;
        core.Submit(Point(CapturedEventKind::Begin, 10U, 1.0, 2.0));
        if (target.Count() != 1U)
        {
            return false;
        }
        const Delivery held{target.Deliveries[0]};
        // The next event drains the retained delivery first, unchanged, then the new one.
        target.Clear();
        target.Decision = DeliveryDecision::Accepted;
        core.Submit(Point(CapturedEventKind::Update, 20U, 3.0, 4.0));
        return target.Count() == 2U && target.Deliveries[0].Sample == held.Sample &&
               target.Deliveries[0].Slot == held.Slot && target.Deliveries[0].Phase == DeliveryPhase::Begin &&
               target.Deliveries[1].Phase == DeliveryPhase::Update;
    }

    /** {brief} A rejecting target abandons the queue and the active contact. */
    [[nodiscard]] bool AbandonsTheContactOnRejection() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{8U, &target};
        core.Start();
        target.Decision = DeliveryDecision::Rejected;
        core.Submit(Point(CapturedEventKind::Begin, 10U, 1.0, 2.0));
        if (target.Count() != 1U)
        {
            return false;
        }
        // The contact was abandoned, so a following update has nothing to extend.
        target.Clear();
        target.Decision = DeliveryDecision::Accepted;
        core.Submit(Point(CapturedEventKind::Update, 20U, 3.0, 4.0));
        return target.Count() == 0U;
    }

    /** {brief} Filling the fixed queue reports Overflow once and stops the session. */
    [[nodiscard]] bool ReportsOverflowAndStops() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{2U, &target};
        core.Start();
        target.Decision = DeliveryDecision::Busy;
        core.Submit(Point(CapturedEventKind::Begin, 10U, 1.0, 2.0));
        core.Submit(Point(CapturedEventKind::Update, 20U, 3.0, 4.0));
        if (target.Failures != 0U)
        {
            return false;
        }
        // The third event cannot be retained by a queue of two slots.
        core.Submit(Point(CapturedEventKind::Update, 30U, 5.0, 6.0));
        if (target.Failures != 1U || target.LastFailure != DeliveryFailure::Overflow)
        {
            return false;
        }
        // The session is stopped, so nothing more is delivered and no second failure is reported.
        target.Clear();
        target.Decision = DeliveryDecision::Accepted;
        core.Submit(Point(CapturedEventKind::Update, 40U, 7.0, 8.0));
        return target.Count() == 0U && target.Failures == 1U;
    }

    /** {brief} Pressure and tilt are clamped to their contract ranges; position is not. */
    [[nodiscard]] bool ClampsOutOfRangeValues() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{8U, &target};
        core.Start();
        CapturedEvent extreme{Point(CapturedEventKind::Begin, 10U, -50.0, 5000.0)};
        extreme.Pressure     = 4.0;
        extreme.TiltXRadians = 10.0;
        extreme.TiltYRadians = -10.0;
        core.Submit(extreme);
        if (target.Count() != 1U)
        {
            return false;
        }
        const Delivery &delivery{target.Deliveries[0]};
        if (delivery.Pressure != 1.0 || delivery.TiltXRadians != HalfPi || delivery.TiltYRadians != -HalfPi)
        {
            return false;
        }
        // Surface position carries no contract range and must not be altered.
        if (delivery.X != -50.0 || delivery.Y != 5000.0)
        {
            return false;
        }
        CapturedEvent negative{Point(CapturedEventKind::Update, 20U, 0.0, 0.0)};
        negative.Pressure = -1.0;
        core.Submit(negative);
        return target.Deliveries[1].Pressure == 0.0;
    }

    /** {brief} Restarting after Stop resets contact state but keeps identities unique. */
    [[nodiscard]] bool ResetsContactStateOnRestart() noexcept
    {
        RecordingTarget   target{};
        AppleDeliveryCore core{8U, &target};
        core.Start();
        core.Submit(Point(CapturedEventKind::Begin, 10U, 1.0, 2.0));
        const std::uint64_t firstContact{target.Deliveries[0].Contact};
        const std::uint64_t firstSample{target.Deliveries[0].Sample};
        core.Stop();
        target.Clear();
        core.Start();
        core.Submit(Point(CapturedEventKind::Begin, 20U, 1.0, 2.0));
        return target.Count() == 1U && target.Deliveries[0].BatchOrdinal == 1U &&
               target.Deliveries[0].Contact != firstContact && target.Deliveries[0].Sample != firstSample;
    }
} // namespace

int main()
{
    return ReportsAnUnusableQueue() && ReportsObservedCapabilities() && IgnoresEventsOutsideASession() &&
                   RequiresAnOpenContact() && DeliversOneContactInOrder() && StartsANewContactAfterEnd() &&
                   CancelsTheOpenContactOnRestart() && DeliversAnExplicitCancel() && CancelsAnOpenContactOnStop() &&
                   RetainsTheDeliveryWhileBusy() && AbandonsTheContactOnRejection() && ReportsOverflowAndStops() &&
                   ClampsOutOfRangeValues() && ResetsContactStateOnRestart()
               ? 0
               : 1;
}
