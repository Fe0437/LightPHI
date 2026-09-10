/**
 * {file}
 * {brief} Defines runtime input capabilities and compile-time mask operations.
 *
 * Samples, delivery state, and backend discovery stay outside this partition.
 */
module;

#include <cstdint>
#include <string_view>

export module lightphi.input:capabilities;

export namespace lightphi::input
{
    /** {brief} One runtime feature reported by an input source. */
    enum class InputCapability : std::uint16_t
    {
        None       = 0U,       ///< Reports no optional input features.
        Pressure   = 1U << 0U, ///< Reports normalized physical force.
        Coalescing = 1U << 1U, ///< Reports earlier observations batched by the platform.
        Prediction = 1U << 2U, ///< Reports provisional observations ahead of the pen.
        Correction = 1U << 3U, ///< Replaces predictions with later observations.
        Timestamp  = 1U << 4U, ///< Reports times from a monotonic clock.
        Tilt       = 1U << 5U, ///< Reports two-axis pen tilt.
        Twist      = 1U << 6U, ///< Reports rotation around the pen axis.
        Eraser     = 1U << 7U, ///< Distinguishes an eraser end from a drawing tip.
        Hover      = 1U << 8U  ///< Reports positions while the pen is above the surface.
    };

    /** {brief} Combine input capabilities into one compile-time mask. */
    [[nodiscard]] constexpr InputCapability operator|(InputCapability left, InputCapability right) noexcept
    {
        return static_cast<InputCapability>(static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
    }

    /** {brief} Select the input capabilities shared by two masks. */
    [[nodiscard]] constexpr InputCapability operator&(InputCapability left, InputCapability right) noexcept
    {
        return static_cast<InputCapability>(static_cast<std::uint16_t>(left) & static_cast<std::uint16_t>(right));
    }

    /** {brief} Mask containing every capability defined by this contract. */
    inline constexpr InputCapability AllInputCapabilities{
        InputCapability::Pressure | InputCapability::Coalescing | InputCapability::Prediction |
        InputCapability::Correction | InputCapability::Timestamp | InputCapability::Tilt | InputCapability::Twist |
        InputCapability::Eraser | InputCapability::Hover};

    /**
     * {brief} Features observed from the selected device and operating-system backend.
     *
     * A missing bit means absent. A source never synthesizes an absent value.
     */
    struct InputSourceCapabilities
    {
        InputCapability Available{};           ///< Runtime features observed from the selected source.
        std::uint32_t   MaximumBatchSamples{}; ///< Largest delivery produced by the source.

        friend constexpr bool operator==(const InputSourceCapabilities &, const InputSourceCapabilities &) = default;
    };

    /** {brief} Whether a source reports every capability in the requested mask. */
    [[nodiscard]] constexpr bool Supports(const InputSourceCapabilities &capabilities,
                                          InputCapability                requested) noexcept
    {
        return (capabilities.Available & requested) == requested;
    }

    /** {brief} Stable name for one capability used by diagnostics. */
    [[nodiscard]] constexpr std::string_view InputCapabilityName(InputCapability capability) noexcept
    {
        switch (capability)
        {
            case InputCapability::None:
                return "None";
            case InputCapability::Pressure:
                return "Pressure";
            case InputCapability::Coalescing:
                return "Coalescing";
            case InputCapability::Prediction:
                return "Prediction";
            case InputCapability::Correction:
                return "Correction";
            case InputCapability::Timestamp:
                return "Timestamp";
            case InputCapability::Tilt:
                return "Tilt";
            case InputCapability::Twist:
                return "Twist";
            case InputCapability::Eraser:
                return "Eraser";
            case InputCapability::Hover:
                return "Hover";
        }
        return "Unknown";
    }
} // namespace lightphi::input
