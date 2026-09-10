#!/usr/bin/env python3
"""Build the LightPHI documentation site.

This is what `just generate-docs` runs, and it is the only entry point.

Nothing it produces is written into `docs/`. The hand-written prose is staged into
`build/docs/src/` and Sphinx builds from there into `build/docs/html/`, so `docs/` contains only
what a person wrote and deleting `build/` removes every generated file.

The Sphinx toolchain is fetched into `build/docs-venv/` on first use rather than expected on the
machine. The toolchain needs a newer Python than an operating system usually supplies, so this
script finds one rather than requiring the caller to be run with it. It therefore has to stay
compatible with that older Python itself. Pass `--no-venv` to use the current interpreter instead,
for a machine that already has the toolchain or an offline image that pre-installed it.

Warnings are errors, so a cross-reference that stops resolving fails this command rather than
quietly degrading the published site.

LightPHI is consumed as a submodule, and a parent project's documentation build looks for the site
at `build/docs/html`. Keep that output path.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.error
import urllib.request

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REQUIREMENTS = HERE / "requirements.txt"
ENVIRONMENT = ROOT / "build/docs-venv"

STAGING = ROOT / "build/docs/src"
OUTPUT = ROOT / "build/docs/html"

# The repository names its documents in upper case; a published site answers on `index.html`.
ENTRY_POINT = ("INDEX.md", "index.md")

PLANTUML_VERSION = "1.2026.7"
PLANTUML_SHA256 = "33aa7ed0ca843e300690230d09268e1f526fdde7e86fecdfa39fb80412cafcde"
PLANTUML_URL = (
    f"https://github.com/plantuml/plantuml/releases/download/v{PLANTUML_VERSION}/plantuml-{PLANTUML_VERSION}.jar"
)
PLANTUML_JAR = ROOT / "build/tools/plantuml" / f"plantuml-{PLANTUML_VERSION}.jar"

# Sphinx 8 and MyST 4 need this. The operating system Python is often older.
MINIMUM_PYTHON = (3, 11)


def _interpreter(environment: Path) -> Path:
    """The Python inside a virtual environment, on either layout."""
    candidate = environment / "bin/python"
    return candidate if candidate.exists() else environment / "Scripts/python.exe"


def _version(interpreter) -> tuple:
    """The (major, minor) of an interpreter, or () when it cannot be asked."""
    try:
        completed = subprocess.run(
            [str(interpreter), "-c", "import sys; print('%d %d' % sys.version_info[:2])"],
            capture_output=True,
            text=True,
            check=True,
            timeout=30,
        )
        return tuple(int(part) for part in completed.stdout.split())
    except (OSError, ValueError, subprocess.SubprocessError):
        return ()


def _base_interpreter() -> str:
    """A Python new enough to host the documentation toolchain.

    The running interpreter is preferred; otherwise the usual named versions and, on macOS, the
    Homebrew installations are tried from newest to oldest.
    """
    candidates = [sys.executable]
    candidates += [f"python3.{minor}" for minor in range(20, 10, -1)]
    candidates += ["python3", "python"]
    if sys.platform == "darwin":
        for root in (Path("/opt/homebrew/opt"), Path("/usr/local/opt")):
            candidates += [str(path / "bin" / "python3") for path in sorted(root.glob("python@*"), reverse=True)]

    seen = set()
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if not resolved or resolved in seen:
            continue
        seen.add(resolved)
        if _version(resolved) >= MINIMUM_PYTHON:
            return resolved

    wanted = "{}.{}".format(*MINIMUM_PYTHON)
    raise SystemExit(
        f"The documentation toolchain needs Python {wanted} or newer, and none was found.\n"
        "  Fix: install it, or pre-install the toolchain and pass --no-venv."
    )


def _ensure_toolchain() -> Path:
    """A Python that has Sphinx, creating and populating the environment when it does not.

    The requirements file is hashed into a stamp beside the environment, so an edit to it
    reinstalls and an unchanged one costs nothing.
    """
    stamp = ENVIRONMENT / ".requirements-sha256"
    wanted = hashlib.sha256(REQUIREMENTS.read_bytes()).hexdigest()
    interpreter = _interpreter(ENVIRONMENT)

    # An environment left behind by a Python too old for the current toolchain is rebuilt rather
    # than reported: the failure it produces otherwise is a wall of unsatisfiable pins.
    if interpreter.exists() and _version(interpreter) < MINIMUM_PYTHON:
        print("[docs] replacing the documentation environment, which uses an unsupported Python")
        shutil.rmtree(ENVIRONMENT)

    interpreter = _interpreter(ENVIRONMENT)
    if interpreter.exists() and stamp.exists() and stamp.read_text(encoding="utf-8").strip() == wanted:
        return interpreter

    if not interpreter.exists():
        print(f"[docs] creating the documentation environment in {ENVIRONMENT.relative_to(ROOT)}")
        subprocess.run([_base_interpreter(), "-m", "venv", str(ENVIRONMENT)], check=True, cwd=str(ROOT))
        interpreter = _interpreter(ENVIRONMENT)

    print("[docs] installing the documentation toolchain (first run only, needs network)")
    subprocess.run(
        [str(interpreter), "-m", "pip", "install", "--quiet", "--disable-pip-version-check", "-r", str(REQUIREMENTS)],
        check=True,
        cwd=str(ROOT),
    )
    stamp.write_text(wanted + "\n", encoding="utf-8")
    return interpreter


def _plantuml_command() -> str:
    """Return the caller's PlantUML command or provide the pinned standalone jar."""
    configured = os.environ.get("PLANTUML_COMMAND")
    if configured:
        return configured
    installed = shutil.which("plantuml")
    if installed:
        return installed
    java = shutil.which("java")
    if java is None:
        raise SystemExit("PlantUML diagrams need Java or a plantuml executable on PATH")
    if not PLANTUML_JAR.is_file():
        PLANTUML_JAR.parent.mkdir(parents=True, exist_ok=True)
        try:
            with urllib.request.urlopen(PLANTUML_URL, timeout=120) as response:
                payload = response.read()
        except (OSError, urllib.error.URLError) as error:
            raise SystemExit(f"Could not download PlantUML from {PLANTUML_URL}: {error}") from error
        if hashlib.sha256(payload).hexdigest() != PLANTUML_SHA256:
            raise SystemExit(f"PlantUML checksum mismatch for {PLANTUML_URL}")
        PLANTUML_JAR.write_bytes(payload)
    return f"{java} -Djava.awt.headless=true -jar {PLANTUML_JAR}"


def _stage_prose() -> None:
    """Copy the hand-written documentation into the staging directory Sphinx reads."""
    if STAGING.exists():
        shutil.rmtree(STAGING)
    shutil.copytree(ROOT / "docs", STAGING, ignore=shutil.ignore_patterns("__pycache__"))
    (STAGING / ENTRY_POINT[0]).rename(STAGING / ENTRY_POINT[1])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-venv", action="store_true", help="use the current interpreter's Sphinx")
    arguments = parser.parse_args()

    interpreter = Path(sys.executable) if arguments.no_venv else _ensure_toolchain()
    _stage_prose()

    subprocess.run(
        [
            sys.executable,
            str(HERE / "generate_api_docs.py"),
            "--output",
            str(STAGING / "api"),
        ],
        check=True,
        cwd=str(ROOT),
    )

    environment = dict(os.environ)
    environment["PLANTUML_COMMAND"] = _plantuml_command()
    subprocess.run(
        [
            str(interpreter),
            "-m",
            "sphinx",
            "-b",
            "html",
            "-W",
            "--keep-going",
            # The configuration is tooling and lives with the scripts, not among the prose, so it
            # is passed rather than found.
            "-c",
            str(HERE),
            str(STAGING),
            str(OUTPUT),
        ],
        check=True,
        cwd=str(ROOT),
        env=environment,
    )
    print(f"[docs] documentation written to {(OUTPUT / 'index.html').relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
