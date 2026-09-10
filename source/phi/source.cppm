/**
 * {file}
 * {brief} Defines contact delivery and the source-to-receiver lifecycle.
 *
 * Operating-system capture, drawing coordinates, and tool behavior stay outside this partition.
 */
module;

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

export module lightphi.input:source;

import :capabilities;
import :samples;

export namespace lightphi::input
{
    /** {brief} Hard upper bound for one source delivery. */
    inline constexpr std::uint32_t MaximumBatchSamples{256U};

    /** {brief} Stable identity of one interaction from begin to end or cancellation. */
    struct ContactId
    {
        std::uint64_t Value{}; ///< Source-local value. Zero denotes no contact.

        friend constexpr bool operator==(ContactId, ContactId) = default;
    };

    /** {brief} Lifecycle position of one delivered contact batch. */
    enum class ContactPhase : std::uint8_t
    {
        Begin,  ///< Opens a new contact and carries at least one sample.
        Update, ///< Extends the active contact and carries at least one sample.
        End,    ///< Closes the active contact and may carry a final sample.
        Cancel  ///< Abandons the active contact and carries no samples.
    };

    /** {brief} Highest defined contact phase, used to reject invented values. */
    inline constexpr ContactPhase LastContactPhase{ContactPhase::Cancel};

    /**
     * {brief} One ordered delivery borrowed for the duration of a receiver call.
     *
     * BatchOrdinal increases strictly within a contact. Begin and Update are nonempty. Cancel is
     * empty. End may contain one final run. One source has at most one active contact.
     */
    struct InputBatch
    {
        ContactId                    Contact{};      ///< Contact extended by this delivery.
        std::uint64_t                BatchOrdinal{}; ///< Strictly increasing delivery ordinal.
        ContactPhase                 Phase{};        ///< Contact lifecycle position.
        std::span<const InputSample> Samples;        ///< Borrowed observations, in sequence order.
    };

    /** {brief} Receiver decision for one synchronous delivery. */
    enum class DeliveryResult : std::uint8_t
    {
        Accepted, ///< The receiver consumed the borrowed batch.
        Busy,     ///< The source must not wait and may retry the exact batch.
        Rejected  ///< The source abandons the active contact.
    };

    /** {brief} Why a source could not start or stopped unexpectedly. */
    enum class InputSourceError : std::uint8_t
    {
        AlreadyStarted, ///< A receiver is already bound.
        Unavailable,    ///< The selected device or surface cannot be reached.
        BackendFailure, ///< The operating-system backend failed while active.
        Overflow        ///< A bounded backend queue filled while the receiver was busy.
    };

    /** {brief} Human-readable explanation for one input source error. */
    [[nodiscard]] constexpr std::string_view InputSourceErrorMessage(InputSourceError error) noexcept
    {
        switch (error)
        {
            case InputSourceError::AlreadyStarted:
                return "the input source is already started";
            case InputSourceError::Unavailable:
                return "the selected input device or host surface is unavailable";
            case InputSourceError::BackendFailure:
                return "the operating-system input backend failed";
            case InputSourceError::Overflow:
                return "the input queue overflowed while the receiver was busy";
        }
        return "the input source failed for an unknown reason";
    }

    /** {brief} Receives borrowed input batches and source failures synchronously. */
    class IInputReceiver
    {
      public:
        IInputReceiver()                                  = default;
        IInputReceiver(const IInputReceiver &)            = delete;
        IInputReceiver(IInputReceiver &&)                 = delete;
        IInputReceiver &operator=(const IInputReceiver &) = delete;
        IInputReceiver &operator=(IInputReceiver &&)      = delete;
        virtual ~IInputReceiver()                         = default;

        /** {brief} Consume one borrowed batch without retaining its span. */
        [[nodiscard]] virtual DeliveryResult Receive(const InputBatch &batch) noexcept = 0;
        /** {brief} Observe a runtime failure after which the source is stopped. */
        virtual void SourceFailed(InputSourceError error) noexcept = 0;
    };

    /** {brief} Delivers observations from one selected pen backend. */
    class IInputSource
    {
      public:
        IInputSource()                                = default;
        IInputSource(const IInputSource &)            = delete;
        IInputSource(IInputSource &&)                 = delete;
        IInputSource &operator=(const IInputSource &) = delete;
        IInputSource &operator=(IInputSource &&)      = delete;
        virtual ~IInputSource()                       = default;

        /** {brief} Report only values observed by the selected device and backend. */
        [[nodiscard]] virtual InputSourceCapabilities Capabilities() const noexcept = 0;
        /** {brief} Whether this source reports every capability in one combined mask. */
        [[nodiscard]] bool SupportsCapabilities(InputCapability requested) const noexcept
        {
            return Supports(Capabilities(), requested);
        }
        /** {brief} Bind a receiver that outlives the active source session. */
        [[nodiscard]] virtual std::expected<void, InputSourceError> Start(IInputReceiver &receiver) = 0;
        /** {brief} Stop delivery, cancel the active contact, and release the receiver. */
        virtual void Stop() noexcept = 0;
    };
} // namespace lightphi::input
