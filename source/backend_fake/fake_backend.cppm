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
#include <string_view>

export module lightphi.backend.fake;

export import lightphi.input;

export import :errors;

namespace lightphi::input::fake
{
    /**
     * {brief} Private state of one fake source, defined in the implementation unit.
     *
     * The name has module linkage: a consumer can hold the pointer but never complete the type.
     */
    class FakeInputSourceImplementation;
} // namespace lightphi::input::fake

export namespace lightphi::input::fake
{
    /** {brief} Highest queue bound accepted by the fake source. */
    inline constexpr std::uint32_t MaximumQueuedBatches{1024U};

    /** {brief} Fixed capabilities and queue bounds for one fake source. */
    struct FakeInputSourceDescriptor
    {
        InputSourceCapabilities Capabilities{};         ///< Values the script is allowed to report.
        std::uint32_t           MaximumQueuedBatches{}; ///< Number of deliveries retained without allocation.
    };

    /**
     * {brief} Preallocated source whose deliveries occur only when explicitly dispatched.
     *
     * Create performs every queue allocation. Enqueue copies into that fixed storage. DispatchNext
     * performs no allocation and invokes the receiver synchronously.
     */
    class FakeInputSource final : public IInputSource
    {
      public:
        ~FakeInputSource() override;

        FakeInputSource(const FakeInputSource &)            = delete;
        FakeInputSource(FakeInputSource &&)                 = delete;
        FakeInputSource &operator=(const FakeInputSource &) = delete;
        FakeInputSource &operator=(FakeInputSource &&)      = delete;

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
        struct ImplementationDeleter
        {
            void operator()(FakeInputSourceImplementation *implementation) const noexcept;
        };
        using Implementation = std::unique_ptr<FakeInputSourceImplementation, ImplementationDeleter>;

        friend std::expected<std::unique_ptr<FakeInputSource>, FakeSourceError>
        CreateFakeInputSource(const FakeInputSourceDescriptor &descriptor) noexcept;

        explicit FakeInputSource(Implementation implementation) noexcept;

        Implementation _implementation;
    };

    /** {brief} Create a stopped fake backend or report why its fixed bounds are unusable. */
    [[nodiscard]] std::expected<std::unique_ptr<FakeInputSource>, FakeSourceError>
    CreateFakeInputSource(const FakeInputSourceDescriptor &descriptor) noexcept;
} // namespace lightphi::input::fake
