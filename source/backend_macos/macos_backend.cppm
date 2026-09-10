/**
 * {file}
 * {brief} Selects the modern macOS pen backend and re-exports LightPHI input.
 *
 * AppKit events, windows, and device masks stay private to the backend.
 */
module;

#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>

export module lightphi.backend;

export import lightphi.input;

namespace lightphi::input
{
    /**
     * {brief} Private state of one selected input source, defined in the implementation unit.
     *
     * The name has module linkage: a consumer can hold the pointer but never complete the type.
     */
    class InputSourceImplementation;
} // namespace lightphi::input

export namespace lightphi::input
{
    /** {brief} Hard upper bound for queued macOS deliveries. */
    inline constexpr std::uint32_t MaximumBackendQueuedBatches{1024U};

    /** {brief} Fixed storage selected before macOS capture starts. */
    struct InputSourceDescriptor
    {
        std::uint32_t MaximumQueuedBatches{64U}; ///< Deliveries retained while the receiver is busy.
    };

    /** {brief} Why the selected operating-system input source could not be created. */
    enum class InputSourceCreationError : std::uint8_t
    {
        InvalidDescriptor, ///< A fixed queue bound is zero or exceeds its hard limit.
        OutOfMemory        ///< Fixed delivery storage could not be allocated.
    };

    /** {brief} Human-readable explanation for one input source creation error. */
    [[nodiscard]] constexpr std::string_view InputSourceCreationErrorMessage(InputSourceCreationError error) noexcept
    {
        switch (error)
        {
            case InputSourceCreationError::InvalidDescriptor:
                return "the input source descriptor contains an invalid queue bound";
            case InputSourceCreationError::OutOfMemory:
                return "the input source could not allocate its fixed storage";
        }
        return "the input source could not be created for an unknown reason";
    }

    /** {brief} Current-platform input source selected by LightPHI's build. */
    class InputSource final : public IInputSource
    {
      public:
        ~InputSource() override;

        InputSource(const InputSource &)            = delete;
        InputSource(InputSource &&)                 = delete;
        InputSource &operator=(const InputSource &) = delete;
        InputSource &operator=(InputSource &&)      = delete;

        /** {brief} Report capabilities observed from the current AppKit tablet device. */
        [[nodiscard]] InputSourceCapabilities Capabilities() const noexcept override;
        /** {brief} Install modern AppKit capture on the calling application's main thread. */
        [[nodiscard]] std::expected<void, InputSourceError> Start(IInputReceiver &receiver) override;
        /** {brief} Remove AppKit capture on the main thread and cancel any active contact. */
        void Stop() noexcept override;

      private:
        struct ImplementationDeleter
        {
            void operator()(InputSourceImplementation *implementation) const noexcept;
        };
        using Implementation = std::unique_ptr<InputSourceImplementation, ImplementationDeleter>;

        friend std::expected<std::unique_ptr<InputSource>, InputSourceCreationError>
        CreateInputSource(const InputSourceDescriptor &descriptor) noexcept;

        explicit InputSource(Implementation implementation) noexcept;

        Implementation _implementation;
    };

    /** {brief} Create the current-platform source with all steady-state storage allocated. */
    [[nodiscard]] std::expected<std::unique_ptr<InputSource>, InputSourceCreationError>
    CreateInputSource(const InputSourceDescriptor &descriptor = {}) noexcept;
} // namespace lightphi::input
