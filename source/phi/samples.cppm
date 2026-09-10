/**
 * {file}
 * {brief} Defines one portable pen observation and how it identifies itself.
 *
 * What makes an observation acceptable is checked beside the capabilities it is checked against.
 * Contact delivery and backend lifecycle stay outside this partition.
 */
module;

#include <cstdint>

export module lightphi.input:samples;

export namespace lightphi::input
{
    /** {brief} Stable identity of one physical or predicted observation within a contact. */
    struct SampleId
    {
        std::uint64_t Value{}; ///< Source-local value. Zero denotes no sample.

        friend constexpr bool operator==(SampleId, SampleId) = default;
    };

    /** {brief} How a backend obtained a sample. */
    enum class SampleOrigin : std::uint8_t
    {
        Measured,  ///< Direct device observation.
        Coalesced, ///< Earlier observation batched by the platform.
        Predicted, ///< Provisional estimate ahead of measured input.
        Corrected  ///< Observation replacing one prediction from the preceding batch.
    };

    /** {brief} Highest defined sample origin, used to reject invented values. */
    inline constexpr SampleOrigin LastSampleOrigin{SampleOrigin::Corrected};

    /**
     * {brief} One pen observation in source-surface coordinates.
     *
     * Id is nonzero and increases strictly in delivery order within a contact. A corrected sample
     * has a new Id, repeats the prediction's Sequence, and names that prediction in Corrects. Other
     * samples leave Corrects zero. Pressure is normalized to [0, 1]. Tilt and twist are radians. A
     * value whose capability is absent remains zero or false.
     */
    struct InputSample
    {
        SampleId      Id{};              ///< Identity of this observation.
        SampleId      Corrects{};        ///< Prediction replaced by this observation, or zero.
        std::uint64_t Sequence{};        ///< Logical position within the contact.
        std::uint64_t TimeNanoseconds{}; ///< Monotonic observation time, or zero when absent.
        double        X{};               ///< Horizontal position in source-surface units.
        double        Y{};               ///< Vertical position in source-surface units.
        double        Pressure{};        ///< Normalized physical force, or zero when absent.
        double        TiltXRadians{};    ///< Horizontal tilt, or zero when absent.
        double        TiltYRadians{};    ///< Vertical tilt, or zero when absent.
        double        TwistRadians{};    ///< Axial rotation, or zero when absent.
        SampleOrigin  Origin{};          ///< Measurement commitment category.
        bool          Eraser{};          ///< Whether the eraser end produced the sample.
        bool          Hovering{};        ///< Whether the pen was above the surface.

        friend constexpr bool operator==(const InputSample &, const InputSample &) = default;
    };
} // namespace lightphi::input
