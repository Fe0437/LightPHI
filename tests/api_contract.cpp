/**
 * {file}
 * {brief} Proves the public LightPHI modules expose the intended C++23 contracts.
 *
 * Runtime source behavior stays in the conformance suite.
 */
#include <concepts>
#include <expected>
#include <memory>
#include <type_traits>

import lightphi.input;
import lightphi.backend.fake;

using namespace lightphi::input;
using namespace lightphi::input::fake;

static_assert(std::is_trivially_copyable_v<SampleId>);
static_assert(std::is_trivially_copyable_v<ContactId>);
static_assert(std::is_trivially_copyable_v<InputSourceCapabilities>);
static_assert(std::is_trivially_copyable_v<InputSample>);
static_assert(std::derived_from<FakeInputSource, IInputSource>);
static_assert(!std::copy_constructible<IInputSource>);
static_assert(!std::move_constructible<IInputSource>);
static_assert(
    std::same_as<decltype(Validate(InputSample{}, InputSourceCapabilities{})), std::expected<void, SampleError>>);
static_assert(std::same_as<decltype(CreateFakeInputSource(FakeInputSourceDescriptor{})),
                           std::expected<std::unique_ptr<FakeInputSource>, FakeSourceError>>);

int main() { return MaximumBatchSamples == 256U && MaximumQueuedBatches == 1024U ? 0 : 1; }
