/**
 * {file}
 * {brief} Locks the initial native layouts used across LightPHI module targets.
 *
 * LightPHI is source-built with its consumer and does not promise cross-toolchain C++ ABI support.
 */
#include <cstddef>
#include <cstdint>
#include <type_traits>

import lightphi.input;
import lightphi.backend.fake;

using namespace lightphi::input;
using namespace lightphi::input::fake;

static_assert(sizeof(void *) == 8U, "LightPHI currently supports 64-bit processes");
static_assert(sizeof(SampleOrigin) == 1U);
static_assert(sizeof(ContactPhase) == 1U);
static_assert(sizeof(InputSourceError) == 1U);
static_assert(sizeof(SampleId) == 8U);
static_assert(sizeof(ContactId) == 8U);
static_assert(sizeof(InputSourceCapabilities) == 16U);
static_assert(sizeof(InputSample) == 88U);
static_assert(offsetof(InputSample, Origin) == 80U);
static_assert(sizeof(InputBatch) == 40U);
static_assert(sizeof(FakeInputSourceDescriptor) == 20U);
static_assert(sizeof(FakeInputSource) == 16U);
static_assert(std::is_standard_layout_v<InputSample>);
static_assert(std::is_standard_layout_v<InputBatch>);

int main() { return 0; }
