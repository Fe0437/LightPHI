/**
 * {file}
 * {brief} Shows capability-aware validation of one portable pen sample.
 *
 * Source creation and event delivery are outside this example.
 */
#include <iostream>

import lightphi.input;

int main()
{
    using namespace lightphi::input;

    constexpr InputSourceCapabilities capabilities{
        .Available           = InputCapability::Pressure | InputCapability::Timestamp,
        .MaximumBatchSamples = 16U,
    };
    constexpr InputSample sample{
        .Id              = {.Value = 1U},
        .Sequence        = 1U,
        .TimeNanoseconds = 42U,
        .X               = 120.0,
        .Y               = 80.0,
        .Pressure        = 0.5,
        .Origin          = SampleOrigin::Measured,
    };

    if (const auto validation{Validate(sample, capabilities)}; !validation)
    {
        const auto &error{validation.error()};
        std::cerr << InputSampleFieldName(error.Field) << ": " << SampleValidationMessage(error.Reason);
        if (error.RequiredCapability != InputCapability::None)
        {
            std::cerr << " (requires " << InputCapabilityName(error.RequiredCapability) << ')';
        }
        std::cerr << '\n';
        return 1;
    }

    std::cout << "Pressure is available and the sample is valid.\n";
    return Supports(capabilities, InputCapability::Pressure) ? 0 : 1;
}
