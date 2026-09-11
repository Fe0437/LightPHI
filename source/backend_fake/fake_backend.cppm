/**
 * {file}
 * {brief} Declares the deterministic bounded LightPHI backend used by tests.
 *
 * Operating-system capture and asynchronous scheduling are deliberately absent.
 */
module;

#include <cstdint>
#include <expected>
#include <memory>

export module lightphi.backend.fake;

export import lightphi.input;

export namespace lightphi::input::fake {
/** {brief} Highest queue bound accepted by the fake source. */
inline constexpr std::uint32_t MaximumQueuedBatches{1024U};

/** {brief} Fixed capabilities and queue bounds for one fake source. */
struct FakeInputSourceDescriptor {
    InputSourceCapabilities Capabilities{}; ///< Values the script is allowed to report.
    std::uint32_t MaximumQueuedBatches{};   ///< Number of deliveries retained without allocation.
};

/** {brief} Why fake construction or script enqueue failed. */
enum class FakeSourceError : std::uint8_t {
    InvalidDescriptor, ///< A queue or sample bound is zero or exceeds its hard limit.
    OutOfMemory,       ///< Fixed storage could not be allocated during creation.
    QueueFull,         ///< All configured batch slots are occupied.
    BatchTooLarge,     ///< A delivery exceeds the source's advertised sample bound.
    InvalidBatch,      ///< Contact, ordinal, sequence, or correction rules are broken.
    UndeclaredValue    ///< A sample reports a capability absent from the descriptor.
};

/** {brief} Why one explicit fake dispatch did not deliver a batch. */
enum class DispatchError : std::uint8_t {
    NotStarted,       ///< No receiver is bound.
    QueueEmpty,       ///< No scripted delivery is waiting.
    ReceiverBusy,     ///< The batch remains queued for an exact retry.
    ReceiverRejected, ///< The queued script and active contact were abandoned.
    BackendFailure    ///< The injected failure stopped the source.
};

/**
 * {brief} Preallocated source whose deliveries occur only when explicitly dispatched.
 *
 * Create performs every queue allocation. Enqueue copies into that fixed storage. DispatchNext
 * performs no allocation and invokes the receiver synchronously.
 */
class FakeInputSource;

/** {brief} Create a stopped fake backend or report why its fixed bounds are unusable. */
[[nodiscard]] std::expected<std::unique_ptr<FakeInputSource>, FakeSourceError>
CreateFakeInputSource(const FakeInputSourceDescriptor &descriptor) noexcept;

class FakeInputSource final : public IInputSource {
  public:
    ~FakeInputSource() override;

    FakeInputSource(const FakeInputSource &) = delete;
    FakeInputSource(FakeInputSource &&) = delete;
    FakeInputSource &operator=(const FakeInputSource &) = delete;
    FakeInputSource &operator=(FakeInputSource &&) = delete;

    /** {brief} Report the scripted capabilities. */
    [[nodiscard]] InputSourceCapabilities Capabilities() const noexcept override;
    /** {brief} Bind the receiver used by explicit dispatch calls. */
    [[nodiscard]] std::expected<void, InputSourceError> Start(IInputReceiver &receiver) override;
    /** {brief} Stop and discard queued deliveries and contact state. */
    void Stop() noexcept override;

    /** {brief} Copy one valid scripted delivery into the fixed queue. */
    [[nodiscard]] std::expected<void, FakeSourceError> Enqueue(const InputBatch &batch) noexcept;
    /** {brief} Deliver or retry the oldest scripted batch synchronously. */
    [[nodiscard]] std::expected<void, DispatchError> DispatchNext() noexcept;
    /** {brief} Make the next dispatch report a runtime backend failure. */
    void FailNext() noexcept;
    /** {brief} Return the number of scripted deliveries waiting for dispatch. */
    [[nodiscard]] std::uint32_t QueuedBatchCount() const noexcept;

  private:
    class Impl;

    friend std::expected<std::unique_ptr<FakeInputSource>, FakeSourceError>
    CreateFakeInputSource(const FakeInputSourceDescriptor &descriptor) noexcept;

    explicit FakeInputSource(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> _implementation;
};
} // namespace lightphi::input::fake
