/**
 * {file}
 * {brief} Why the fake backend refused a script or a dispatch.
 *
 * A vocabulary of its own because it is shared: the rules a delivery must satisfy raise these, the
 * source that dispatches them raises these, and a test reads them. None of those needs to know how
 * the others work in order to name a failure.
 */
module;

#include <cstdint>
#include <string_view>

export module lightphi.backend.fake:errors;

export namespace lightphi::input::fake
{
    /** {brief} Why fake construction or script enqueue failed. */
    enum class FakeSourceError : std::uint8_t
    {
        InvalidDescriptor, ///< A queue or sample bound is zero or exceeds its hard limit.
        OutOfMemory,       ///< Fixed storage could not be allocated during creation.
        QueueFull,         ///< All configured batch slots are occupied.
        BatchTooLarge,     ///< A delivery exceeds the source's advertised sample bound.
        InvalidBatch,      ///< Contact, ordinal, sequence, or correction rules are broken.
        UndeclaredValue    ///< A sample reports a capability absent from the descriptor.
    };

    /** {brief} Human-readable explanation for one fake source error. */
    [[nodiscard]] constexpr std::string_view FakeSourceErrorMessage(FakeSourceError error) noexcept
    {
        switch (error)
        {
            case FakeSourceError::InvalidDescriptor:
                return "the fake source descriptor contains an invalid bound or capability mask";
            case FakeSourceError::OutOfMemory:
                return "the fake source could not allocate its fixed storage";
            case FakeSourceError::QueueFull:
                return "the fake source queue is full";
            case FakeSourceError::BatchTooLarge:
                return "the batch exceeds the source sample bound";
            case FakeSourceError::InvalidBatch:
                return "the batch breaks the contact, ordering, or correction rules";
            case FakeSourceError::UndeclaredValue:
                return "a sample uses a capability absent from the fake source descriptor";
        }
        return "the fake source failed for an unknown reason";
    }

    /** {brief} Why one explicit fake dispatch did not deliver a batch. */
    enum class DispatchError : std::uint8_t
    {
        NotStarted,       ///< No receiver is bound.
        QueueEmpty,       ///< No scripted delivery is waiting.
        ReceiverBusy,     ///< The batch remains queued for an exact retry.
        ReceiverRejected, ///< The queued script and active contact were abandoned.
        BackendFailure    ///< The injected failure stopped the source.
    };

    /** {brief} Human-readable explanation for one fake dispatch error. */
    [[nodiscard]] constexpr std::string_view DispatchErrorMessage(DispatchError error) noexcept
    {
        switch (error)
        {
            case DispatchError::NotStarted:
                return "the fake source has not been started";
            case DispatchError::QueueEmpty:
                return "the fake source has no queued delivery";
            case DispatchError::ReceiverBusy:
                return "the receiver is busy and the exact delivery remains queued";
            case DispatchError::ReceiverRejected:
                return "the receiver rejected the delivery";
            case DispatchError::BackendFailure:
                return "the injected backend failure stopped the fake source";
        }
        return "the fake dispatch failed for an unknown reason";
    }
} // namespace lightphi::input::fake
