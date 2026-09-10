/**
 * {file}
 * {brief} Shows backend-agnostic use of the automatically selected LightPHI source.
 *
 * Backend selection and operating-system event handling stay outside this example.
 */
#include <iostream>

import lightphi.backend;

namespace
{
    class Receiver final : public lightphi::input::IInputReceiver
    {
      public:
        [[nodiscard]] lightphi::input::DeliveryResult
        Receive(const lightphi::input::InputBatch &batch) noexcept override
        {
            std::cout << "contact " << batch.Contact.Value << ", batch " << batch.BatchOrdinal << ", samples "
                      << batch.Samples.size() << '\n';
            return lightphi::input::DeliveryResult::Accepted;
        }

        void SourceFailed(const lightphi::input::InputSourceError error) noexcept override
        {
            std::cerr << lightphi::input::InputSourceErrorMessage(error) << '\n';
        }
    };
} // namespace

int main()
{
    using namespace lightphi::input;

    auto sourceResult{CreateInputSource()};
    if (!sourceResult)
    {
        std::cerr << InputSourceCreationErrorMessage(sourceResult.error()) << '\n';
        return 1;
    }

    auto      &source{*sourceResult};
    const auto capabilities{source->Capabilities()};
    std::cout << "maximum samples per batch: " << capabilities.MaximumBatchSamples << '\n';
    const auto penFeatures{InputCapability::Pressure | InputCapability::Tilt | InputCapability::Eraser};
    std::cout << "pressure, tilt, and eraser: " << (source->SupportsCapabilities(penFeatures) ? "yes" : "no") << '\n';

    Receiver   receiver{};
    const auto started{source->Start(receiver)};
    if (!started)
    {
        if (started.error() == InputSourceError::Unavailable)
        {
            std::cout << InputSourceErrorMessage(started.error()) << '\n';
            return 0;
        }
        std::cerr << InputSourceErrorMessage(started.error()) << '\n';
        return 1;
    }
    source->Stop();
    return 0;
}
