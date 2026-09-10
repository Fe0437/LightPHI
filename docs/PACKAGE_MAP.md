# Package map

This table is the complete LightPHI package inventory.

| Package and target | Path | Responsibility | Dependencies | Status |
|---|---|---|---|---|
| Input contract · `LightPHI::LightPHI` · `lightphi.input` | `source/phi` | Re-export capability, sample, and source-delivery partitions as one stable input API | C++23 standard library | Implemented |
| Fake backend · `LightPHI::FakeBackend` · `lightphi.backend.fake` | `source/backend_fake` | Re-export the input contract and provide the preallocated scripted backend factory | Input contract, Microsoft GSL | Implemented |
| Shared Apple input delivery · private `LightPHIAppleInput` | `source/backend_apple` | Normalize Apple adapter values into contacts and retain bounded deliveries across busy retries | C++23 standard library, Microsoft GSL | Implemented |
| macOS backend · `LightPHI::Backend` · `lightphi.backend` | `source/backend_macos` | Re-export the input contract and capture modern AppKit tablet events through Swift | Input contract, shared Apple delivery, Microsoft GSL, Swift, AppKit | Implemented |
| iOS backend · future `LightPHI::Backend` · future `lightphi.backend` | Not present | Attach UIKit touch capture to an application-owned view and reuse shared Apple delivery | Input contract, shared Apple delivery, Swift, UIKit | Future |
