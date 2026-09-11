/**
 * {file}
 * {brief} Defines portable pen observations and the source-to-receiver boundary.
 *
 * Operating-system events, drawing coordinates, tool behavior, and presentation stay outside
 * this module.
 */
module;

#include <cmath>
#include <cstdint>
#include <expected>
#include <span>

export module lightphi.input;

export namespace lightphi::input {
/** {brief} Hard upper bound for one source delivery. */
inline constexpr std::uint32_t MaximumBatchSamples{256U};

/** {brief} Stable identity of one physical or predicted observation within a contact. */
struct SampleId {
    std::uint64_t Value{}; ///< Source-local value. Zero denotes no sample.

    friend constexpr bool operator==(SampleId, SampleId) = default;
};

/** {brief} Stable identity of one interaction from begin to end or cancellation. */
struct ContactId {
    std::uint64_t Value{}; ///< Source-local value. Zero denotes no contact.

    friend constexpr bool operator==(ContactId, ContactId) = default;
};

/** {brief} How a backend obtained a sample. */
enum class SampleOrigin : std::uint8_t {
    Measured,  ///< Direct device observation.
    Coalesced, ///< Earlier observation batched by the platform.
    Predicted, ///< Provisional estimate ahead of measured input.
    Corrected  ///< Observation replacing one prediction from the preceding batch.
};

/** {brief} Highest defined sample origin, used to reject invented values. */
inline constexpr SampleOrigin LastSampleOrigin{SampleOrigin::Corrected};

/** {brief} Lifecycle position of one delivered contact batch. */
enum class ContactPhase : std::uint8_t {
    Begin,  ///< Opens a new contact and carries at least one sample.
    Update, ///< Extends the active contact and carries at least one sample.
    End,    ///< Closes the active contact and may carry a final sample.
    Cancel  ///< Abandons the active contact and carries no samples.
};

/** {brief} Highest defined contact phase, used to reject invented values. */
inline constexpr ContactPhase LastContactPhase{ContactPhase::Cancel};

/**
 * {brief} Features observed from the selected device and operating-system backend.
 *
 * False means absent. A source never synthesizes an absent value.
 */
struct InputSourceCapabilities {
    bool Pressure{};                     ///< Reports normalized physical force.
    bool Coalescing{};                   ///< Reports earlier observations batched by the platform.
    bool Prediction{};                   ///< Reports provisional observations ahead of the pen.
    bool Correction{};                   ///< Replaces predictions with later observations.
    bool Timestamp{};                    ///< Reports times from a monotonic clock.
    bool Tilt{};                         ///< Reports two-axis pen tilt.
    bool Twist{};                        ///< Reports rotation around the pen axis.
    bool Eraser{};                       ///< Distinguishes an eraser end from a drawing tip.
    bool Hover{};                        ///< Reports positions while the pen is above the surface.
    std::uint32_t MaximumBatchSamples{}; ///< Largest delivery produced by the source.

    friend constexpr bool operator==(const InputSourceCapabilities &, const InputSourceCapabilities &) = default;
};

/**
 * {brief} One pen observation in source-surface coordinates.
 *
 * Id is nonzero and increases strictly in delivery order within a contact. A corrected sample
 * has a new Id, repeats the prediction's Sequence, and names that prediction in Corrects. Other
 * samples leave Corrects zero. Pressure is normalized to [0, 1]. Tilt and twist are radians. A
 * value whose capability is absent remains zero or false.
 */
struct InputSample {
    SampleId Id{};                   ///< Identity of this observation.
    SampleId Corrects{};             ///< Prediction replaced by this observation, or zero.
    std::uint64_t Sequence{};        ///< Logical position within the contact.
    std::uint64_t TimeNanoseconds{}; ///< Monotonic observation time, or zero when absent.
    double X{};                      ///< Horizontal position in source-surface units.
    double Y{};                      ///< Vertical position in source-surface units.
    double Pressure{};               ///< Normalized physical force, or zero when absent.
    double TiltXRadians{};           ///< Horizontal tilt, or zero when absent.
    double TiltYRadians{};           ///< Vertical tilt, or zero when absent.
    double TwistRadians{};           ///< Axial rotation, or zero when absent.
    SampleOrigin Origin{};           ///< Measurement commitment category.
    bool Eraser{};                   ///< Whether the eraser end produced the sample.
    bool Hovering{};                 ///< Whether the pen was above the surface.

    friend constexpr bool operator==(const InputSample &, const InputSample &) = default;
};

/** {brief} Why one sample does not satisfy the portable source contract. */
enum class SampleError : std::uint8_t {
    InvalidIdentity,   ///< The sample identity or correction identity is invalid.
    InvalidValue,      ///< A coordinate or reported physical value is outside its range.
    UnsupportedOrigin, ///< The origin value is not defined.
    UndeclaredValue    ///< The sample reports a capability the source did not declare.
};

/** {brief} Validate one sample against the capabilities declared by its source. */
[[nodiscard]] inline std::expected<void, SampleError> Validate(const InputSample &sample,
                                                               const InputSourceCapabilities &capabilities) noexcept {
    if (sample.Id.Value == 0U || (sample.Origin == SampleOrigin::Corrected) != (sample.Corrects.Value != 0U)) {
        return std::unexpected{SampleError::InvalidIdentity};
    }
    if (sample.Origin > LastSampleOrigin) {
        return std::unexpected{SampleError::UnsupportedOrigin};
    }
    if (!std::isfinite(sample.X) || !std::isfinite(sample.Y) || !std::isfinite(sample.Pressure) ||
        !std::isfinite(sample.TiltXRadians) || !std::isfinite(sample.TiltYRadians) ||
        !std::isfinite(sample.TwistRadians) || sample.Pressure < 0.0 || sample.Pressure > 1.0) {
        return std::unexpected{SampleError::InvalidValue};
    }
    const bool undeclared{(!capabilities.Pressure && sample.Pressure != 0.0) ||
                          (!capabilities.Timestamp && sample.TimeNanoseconds != 0U) ||
                          (!capabilities.Tilt && (sample.TiltXRadians != 0.0 || sample.TiltYRadians != 0.0)) ||
                          (!capabilities.Twist && sample.TwistRadians != 0.0) ||
                          (!capabilities.Eraser && sample.Eraser) || (!capabilities.Hover && sample.Hovering) ||
                          (sample.Origin == SampleOrigin::Coalesced && !capabilities.Coalescing) ||
                          (sample.Origin == SampleOrigin::Predicted && !capabilities.Prediction) ||
                          (sample.Origin == SampleOrigin::Corrected && !capabilities.Correction)};
    if (undeclared) {
        return std::unexpected{SampleError::UndeclaredValue};
    }
    return {};
}

/**
 * {brief} One ordered delivery borrowed for the duration of a receiver call.
 *
 * BatchOrdinal increases strictly within a contact. Begin and Update are nonempty. Cancel is
 * empty. End may contain one final run. One source has at most one active contact.
 */
struct InputBatch {
    ContactId Contact{};                  ///< Contact extended by this delivery.
    std::uint64_t BatchOrdinal{};         ///< Strictly increasing delivery ordinal.
    ContactPhase Phase{};                 ///< Contact lifecycle position.
    std::span<const InputSample> Samples; ///< Borrowed observations, in sequence order.
};

/** {brief} Receiver decision for one synchronous delivery. */
enum class DeliveryResult : std::uint8_t {
    Accepted, ///< The receiver consumed the borrowed batch.
    Busy,     ///< The source must not wait and may retry the exact batch.
    Rejected  ///< The source abandons the active contact.
};

/** {brief} Why a source could not start or stopped unexpectedly. */
enum class InputSourceError : std::uint8_t {
    AlreadyStarted, ///< A receiver is already bound.
    Unavailable,    ///< The selected device or surface cannot be reached.
    BackendFailure  ///< The operating-system backend failed while active.
};

/** {brief} Receives borrowed input batches and source failures synchronously. */
class IInputReceiver {
  public:
    IInputReceiver() = default;
    IInputReceiver(const IInputReceiver &) = delete;
    IInputReceiver(IInputReceiver &&) = delete;
    IInputReceiver &operator=(const IInputReceiver &) = delete;
    IInputReceiver &operator=(IInputReceiver &&) = delete;
    virtual ~IInputReceiver() = default;

    /** {brief} Consume one borrowed batch without retaining its span. */
    [[nodiscard]] virtual DeliveryResult Receive(const InputBatch &batch) noexcept = 0;
    /** {brief} Observe a runtime failure after which the source is stopped. */
    virtual void SourceFailed(InputSourceError error) noexcept = 0;
};

/** {brief} Delivers observations from one selected pen backend. */
class IInputSource {
  public:
    IInputSource() = default;
    IInputSource(const IInputSource &) = delete;
    IInputSource(IInputSource &&) = delete;
    IInputSource &operator=(const IInputSource &) = delete;
    IInputSource &operator=(IInputSource &&) = delete;
    virtual ~IInputSource() = default;

    /** {brief} Report only values observed by the selected device and backend. */
    [[nodiscard]] virtual InputSourceCapabilities Capabilities() const noexcept = 0;
    /** {brief} Bind a receiver that outlives the active source session. */
    [[nodiscard]] virtual std::expected<void, InputSourceError> Start(IInputReceiver &receiver) = 0;
    /** {brief} Stop delivery, cancel the active contact, and release the receiver. */
    virtual void Stop() noexcept = 0;
};
} // namespace lightphi::input
