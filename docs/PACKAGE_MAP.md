# Package map

This table is the complete LightPHI package inventory.

| Package and target | Path | Responsibility | Dependencies | Status |
|---|---|---|---|---|
| Input contract · `LightPHI::LightPHI` · `lightphi.input` | `source/input.cppm` | Stable capabilities, samples, contacts, lifecycle, errors, and source/receiver interfaces | C++23 standard library | Implemented |
| Fake backend · `LightPHI::FakeBackend` · `lightphi.backend.fake` | `source/backend_fake` | Re-export the input contract and provide the preallocated scripted backend factory | Input contract | Implemented |
| Platform backend · planned `LightPHI::Backend` · planned `lightphi.backend` | Not present | Re-export the input contract and provide the selected platform source factory; macOS is first | Input contract, one platform framework at the private edge | Future |

The input contract must not depend on the fake backend, a platform backend, Flexible
Drawing, or an application. The fake is not a fallback production backend.
