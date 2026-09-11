"""Check public-project files and module boundaries required by LightPHI."""

from __future__ import annotations

import pathlib
import sys


REQUIRED_FILES = (
    "ARCHITECTURE.md",
    "API_GUIDELINES.md",
    "CONTRIBUTING.md",
    "GOVERNANCE.md",
    "LICENSE",
    "NOTICE",
    "README.md",
    "SECURITY.md",
    "docs/PACKAGE_MAP.md",
    "docs/abi/v1.json",
    "source/input.cppm",
    "source/backend_fake/fake_backend.cppm",
)


def main() -> int:
    root = pathlib.Path(sys.argv[1]).resolve()
    missing = [relative for relative in REQUIRED_FILES if not (root / relative).is_file()]
    if missing:
        print("Missing required project files:")
        for relative in missing:
            print(f"- {relative}")
        return 1

    input_module = (root / "source/input.cppm").read_text(encoding="utf-8")
    required_contract = ("export module lightphi.input;", "class IInputSource", "class IInputReceiver")
    if any(token not in input_module for token in required_contract):
        print("The input module is missing its required exported contract")
        return 1
    forbidden = ("FlexibleDrawing", "AppKit", "NSEvent", "extern \"C\"", "LP_API")
    found = [token for token in forbidden if token in input_module]
    if found:
        print(f"The input module contains forbidden dependencies: {', '.join(found)}")
        return 1

    fake_backend = (root / "source/backend_fake/fake_backend.cppm").read_text(encoding="utf-8")
    fake_contract = ("export module lightphi.backend.fake;", "export import lightphi.input;", "CreateFakeInputSource")
    if any(token not in fake_backend for token in fake_contract):
        print("The fake backend does not re-export the input contract and its factory")
        return 1

    licence = (root / "LICENSE").read_text(encoding="utf-8")
    if "Mozilla Public License Version 2.0" not in licence:
        print("LICENSE is not the MPL-2.0 text")
        return 1
    notice = (root / "NOTICE").read_text(encoding="utf-8")
    if "Copyright (c) 2026 Federico Forti" not in notice or "github.com/Fe0437/LightPHI" not in notice:
        print("NOTICE is missing the project attribution")
        return 1
    contributing = (root / "CONTRIBUTING.md").read_text(encoding="utf-8")
    contribution_terms = ("MPL-2.0", "Signed-off-by:", "Developer Certificate of Origin 1.1")
    if any(term not in contributing for term in contribution_terms):
        print("CONTRIBUTING.md is missing the accepted contribution terms")
        return 1
    governance = (root / "GOVERNANCE.md").read_text(encoding="utf-8").lower()
    if "release owner" not in governance or "approving maintainer" not in governance:
        print("GOVERNANCE.md is missing ownership or review policy")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
