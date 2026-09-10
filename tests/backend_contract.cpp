/**
 * {file}
 * {brief} Proves the automatically selected backend exposes the portable C++23 API.
 *
 * Backend implementation details and physical input stay outside this test.
 */
#include <concepts>
#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <utility>

import lightphi.backend;

using namespace lightphi::input;

static_assert(std::derived_from<InputSource, IInputSource>);
static_assert(
    std::same_as<decltype(CreateInputSource()), std::expected<std::unique_ptr<InputSource>, InputSourceCreationError>>);
static_assert(
    std::same_as<decltype(std::declval<const IInputSource &>().SupportsCapabilities(InputCapability::None)), bool>);

int main()
{
    const auto invalid{CreateInputSource({.MaximumQueuedBatches = 0U})};
    const auto source{CreateInputSource({.MaximumQueuedBatches = 2U})};
    if (invalid || invalid.error() != InputSourceCreationError::InvalidDescriptor ||
        InputSourceCreationErrorMessage(invalid.error()).empty() || !source)
    {
        return 1;
    }

    const auto capabilities{(*source)->Capabilities()};
    const auto requested{InputCapability::Pressure | InputCapability::Tilt | InputCapability::Eraser};
    if ((*source)->SupportsCapabilities(requested) != Supports(capabilities, requested))
    {
        return 1;
    }
    const auto available{static_cast<std::uint16_t>(capabilities.Available)};
    const auto known{static_cast<std::uint16_t>(AllInputCapabilities)};
    return (available & static_cast<std::uint16_t>(~known)) == 0U && capabilities.MaximumBatchSamples > 0U &&
                   capabilities.MaximumBatchSamples <= MaximumBatchSamples
               ? 0
               : 1;
}
