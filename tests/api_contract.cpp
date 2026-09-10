/**
 * {file}
 * {brief} Proves the public LightPHI modules expose the intended C++23 contracts.
 *
 * Runtime source behavior stays in the conformance suite.
 */
#include <concepts>
#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>

import lightphi.input;

using namespace lightphi::input;

static_assert(std::is_trivially_copyable_v<SampleId>);
static_assert(std::is_trivially_copyable_v<ContactId>);
static_assert(sizeof(InputCapability) == sizeof(std::uint16_t));
static_assert(std::is_trivially_copyable_v<InputSourceCapabilities>);
static_assert(std::is_trivially_copyable_v<InputSample>);
static_assert(!std::copy_constructible<IInputSource>);
static_assert(!std::move_constructible<IInputSource>);
static_assert(std::same_as<decltype(Validate(InputSample{}, InputSourceCapabilities{})),
                           std::expected<void, SampleValidationError>>);
inline constexpr InputSourceCapabilities PenCapabilities{.Available = InputCapability::Pressure | InputCapability::Tilt,
                                                         .MaximumBatchSamples = 8U};
static_assert(Supports(PenCapabilities, InputCapability::Pressure));
static_assert(Supports(PenCapabilities, InputCapability::Pressure | InputCapability::Tilt));
static_assert(!Supports(PenCapabilities, InputCapability::Prediction));
int main()
{
    constexpr InputSourceCapabilities capabilities{
        .Available           = InputCapability::Timestamp,
        .MaximumBatchSamples = 1U,
    };
    constexpr InputSample sample{
        .Id              = {.Value = 1U},
        .Sequence        = 1U,
        .TimeNanoseconds = 42U,
        .Pressure        = 0.5,
    };
    const auto validation{Validate(sample, capabilities)};
    if (validation || validation.error() != SampleValidationError{
                                                .Reason             = SampleValidationReason::MissingCapability,
                                                .Field              = InputSampleField::Pressure,
                                                .RequiredCapability = InputCapability::Pressure,
                                            })
    {
        return 1;
    }
    return MaximumBatchSamples == 256U && InputSampleFieldName(validation.error().Field) == "Pressure" &&
                   InputCapabilityName(validation.error().RequiredCapability) == "Pressure"
               ? 0
               : 1;
}
