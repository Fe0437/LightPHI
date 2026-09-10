/**
 * {file}
 * {brief} Checks one observation against the capabilities its source reports.
 *
 * Separate from the observation itself because it answers a different question: the schema says what
 * a sample carries, this says whether a source was entitled to carry it. A field populated by a
 * device that does not report it is the failure worth naming, and naming it is what these are for.
 */
module;

#include <cmath>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <string_view>
#include <tuple>
#include <utility>

export module lightphi.input:validation;

import :capabilities;
import :samples;

export namespace lightphi::input
{
    /** {brief} One field inspected while validating an input sample. */
    enum class InputSampleField : std::uint8_t
    {
        None,            ///< No individual field caused the error.
        Id,              ///< InputSample::Id.
        Corrects,        ///< InputSample::Corrects.
        Origin,          ///< InputSample::Origin.
        TimeNanoseconds, ///< InputSample::TimeNanoseconds.
        X,               ///< InputSample::X.
        Y,               ///< InputSample::Y.
        Pressure,        ///< InputSample::Pressure.
        TiltXRadians,    ///< InputSample::TiltXRadians.
        TiltYRadians,    ///< InputSample::TiltYRadians.
        TwistRadians,    ///< InputSample::TwistRadians.
        Eraser,          ///< InputSample::Eraser.
        Hovering         ///< InputSample::Hovering.
    };

    /** {brief} Why one sample does not satisfy the portable source contract. */
    enum class SampleValidationReason : std::uint8_t
    {
        ZeroIdentity,                 ///< The sample identity is zero.
        MissingCorrectionIdentity,    ///< A corrected sample does not name the prediction it replaces.
        UnexpectedCorrectionIdentity, ///< A sample that is not corrected names another sample.
        UnsupportedOrigin,            ///< The sample origin value is not defined.
        NonFiniteValue,               ///< A numeric sample field is not finite.
        PressureOutOfRange,           ///< Pressure is outside the normalized [0, 1] range.
        MissingCapability             ///< A populated field requires a capability the source does not report.
    };

    /** {brief} Actionable detail for one rejected input sample. */
    struct SampleValidationError
    {
        SampleValidationReason Reason{};             ///< Why validation failed.
        InputSampleField       Field{};              ///< Field that caused the failure.
        InputCapability        RequiredCapability{}; ///< Missing capability, or None for a value error.

        friend constexpr bool operator==(const SampleValidationError &, const SampleValidationError &) = default;
    };

    /** {brief} Stable field name suitable for validation diagnostics. */
    [[nodiscard]] constexpr std::string_view InputSampleFieldName(InputSampleField field) noexcept
    {
        switch (field)
        {
            case InputSampleField::None:
                return "None";
            case InputSampleField::Id:
                return "Id";
            case InputSampleField::Corrects:
                return "Corrects";
            case InputSampleField::Origin:
                return "Origin";
            case InputSampleField::TimeNanoseconds:
                return "TimeNanoseconds";
            case InputSampleField::X:
                return "X";
            case InputSampleField::Y:
                return "Y";
            case InputSampleField::Pressure:
                return "Pressure";
            case InputSampleField::TiltXRadians:
                return "TiltXRadians";
            case InputSampleField::TiltYRadians:
                return "TiltYRadians";
            case InputSampleField::TwistRadians:
                return "TwistRadians";
            case InputSampleField::Eraser:
                return "Eraser";
            case InputSampleField::Hovering:
                return "Hovering";
        }
        return "Unknown";
    }

    /** {brief} Human-readable explanation for one sample validation reason. */
    [[nodiscard]] constexpr std::string_view SampleValidationMessage(SampleValidationReason reason) noexcept
    {
        switch (reason)
        {
            case SampleValidationReason::ZeroIdentity:
                return "the sample identity is zero";
            case SampleValidationReason::MissingCorrectionIdentity:
                return "the corrected sample does not name the prediction it replaces";
            case SampleValidationReason::UnexpectedCorrectionIdentity:
                return "a sample that is not corrected names another sample";
            case SampleValidationReason::UnsupportedOrigin:
                return "the sample origin is not defined";
            case SampleValidationReason::NonFiniteValue:
                return "the numeric value is not finite";
            case SampleValidationReason::PressureOutOfRange:
                return "pressure is outside the normalized range";
            case SampleValidationReason::MissingCapability:
                return "the sample field requires a capability the source does not report";
        }
        return "the sample is invalid";
    }

    /** {brief} Validate one sample against the capabilities declared by its source. */
    [[nodiscard]] inline std::expected<void, SampleValidationError>
    Validate(const InputSample &sample, const InputSourceCapabilities &capabilities) noexcept
    {
        if (sample.Id.Value == 0U)
        {
            return std::unexpected{SampleValidationError{
                .Reason             = SampleValidationReason::ZeroIdentity,
                .Field              = InputSampleField::Id,
                .RequiredCapability = InputCapability::None,
            }};
        }
        if (sample.Origin == SampleOrigin::Corrected && sample.Corrects.Value == 0U)
        {
            return std::unexpected{SampleValidationError{
                .Reason             = SampleValidationReason::MissingCorrectionIdentity,
                .Field              = InputSampleField::Corrects,
                .RequiredCapability = InputCapability::None,
            }};
        }
        if (sample.Origin != SampleOrigin::Corrected && sample.Corrects.Value != 0U)
        {
            return std::unexpected{SampleValidationError{
                .Reason             = SampleValidationReason::UnexpectedCorrectionIdentity,
                .Field              = InputSampleField::Corrects,
                .RequiredCapability = InputCapability::None,
            }};
        }
        if (sample.Origin > LastSampleOrigin)
        {
            return std::unexpected{SampleValidationError{
                .Reason             = SampleValidationReason::UnsupportedOrigin,
                .Field              = InputSampleField::Origin,
                .RequiredCapability = InputCapability::None,
            }};
        }

        const auto requireFinite{
            [](const double value, const InputSampleField field) -> std::expected<void, SampleValidationError>
            {
                if (!std::isfinite(value))
                {
                    return std::unexpected{SampleValidationError{
                        .Reason             = SampleValidationReason::NonFiniteValue,
                        .Field              = field,
                        .RequiredCapability = InputCapability::None,
                    }};
                }
                return {};
            }};
        for (const auto [value, field] :
             {std::pair{sample.X, InputSampleField::X}, std::pair{sample.Y, InputSampleField::Y},
              std::pair{sample.Pressure, InputSampleField::Pressure},
              std::pair{sample.TiltXRadians, InputSampleField::TiltXRadians},
              std::pair{sample.TiltYRadians, InputSampleField::TiltYRadians},
              std::pair{sample.TwistRadians, InputSampleField::TwistRadians}})
        {
            if (const auto finite{requireFinite(value, field)}; !finite)
            {
                return finite;
            }
        }
        if (sample.Pressure < 0.0 || sample.Pressure > 1.0)
        {
            return std::unexpected{SampleValidationError{
                .Reason             = SampleValidationReason::PressureOutOfRange,
                .Field              = InputSampleField::Pressure,
                .RequiredCapability = InputCapability::None,
            }};
        }

        const auto requireCapability{
            [&capabilities](const bool populated, const InputSampleField field,
                            const InputCapability capability) -> std::expected<void, SampleValidationError>
            {
                if (populated && !Supports(capabilities, capability))
                {
                    return std::unexpected{SampleValidationError{
                        .Reason             = SampleValidationReason::MissingCapability,
                        .Field              = field,
                        .RequiredCapability = capability,
                    }};
                }
                return {};
            }};
        const std::initializer_list<std::tuple<bool, InputSampleField, InputCapability>> requirements{
            std::tuple{sample.Pressure != 0.0, InputSampleField::Pressure, InputCapability::Pressure},
            std::tuple{sample.TimeNanoseconds != 0U, InputSampleField::TimeNanoseconds, InputCapability::Timestamp},
            std::tuple{sample.TiltXRadians != 0.0, InputSampleField::TiltXRadians, InputCapability::Tilt},
            std::tuple{sample.TiltYRadians != 0.0, InputSampleField::TiltYRadians, InputCapability::Tilt},
            std::tuple{sample.TwistRadians != 0.0, InputSampleField::TwistRadians, InputCapability::Twist},
            std::tuple{sample.Eraser, InputSampleField::Eraser, InputCapability::Eraser},
            std::tuple{sample.Hovering, InputSampleField::Hovering, InputCapability::Hover},
            std::tuple{sample.Origin == SampleOrigin::Coalesced, InputSampleField::Origin, InputCapability::Coalescing},
            std::tuple{sample.Origin == SampleOrigin::Predicted, InputSampleField::Origin, InputCapability::Prediction},
            std::tuple{sample.Origin == SampleOrigin::Corrected, InputSampleField::Origin, InputCapability::Correction},
        };
        for (const auto &[populated, field, capability] : requirements)
        {
            if (const auto supported{requireCapability(populated, field, capability)}; !supported)
            {
                return supported;
            }
        }
        return {};
    }
} // namespace lightphi::input
