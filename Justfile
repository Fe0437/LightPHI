# LightPHI developer entry points.
set shell := ["sh", "-cu"]
set windows-shell := ["powershell.exe", "-NoProfile", "-Command"]

# List every public developer command with its short description.
default:
    @just --list

[private]
_compile-release:
    cmake --preset release
    cmake --build --preset release --parallel

[private]
_compile-debug:
    cmake --preset debug
    cmake --build --preset debug --parallel

# Compile the optimized Release configuration without tests or examples.
build: _compile-release

# Compile the Debug configuration with debug symbols without running tests.
debug: _compile-debug

# Compile the Debug configuration, then run the smoke-labeled tests.
debug-smoke: _compile-debug
    ctest --preset debug -L smoke

# Compile the Debug configuration, then run the complete test suite once.
debug-test: _compile-debug
    ctest --preset debug

[private]
_compile-asan:
    cmake --preset dev-asan
    cmake --build --preset dev-asan --parallel

# Build with ASan+UBSan and run the smoke-labeled tests.
build-asan: _compile-asan
    ctest --preset dev-asan -L smoke

# Build with ASan+UBSan and run the complete test suite.
test-asan: _compile-asan
    ctest --preset dev-asan

[private]
_compile-ubsan:
    cmake --preset dev-ubsan
    cmake --build --preset dev-ubsan --parallel

# Build with UBSan and run the smoke-labeled tests.
build-ubsan: _compile-ubsan
    ctest --preset dev-ubsan -L smoke

# Build with UBSan and run the complete test suite.
test-ubsan: _compile-ubsan
    ctest --preset dev-ubsan

[private]
_compile-tsan:
    cmake --preset dev-tsan
    cmake --build --preset dev-tsan --parallel

# Build with ThreadSanitizer and run the complete test suite.
test-tsan: _compile-tsan
    ctest --preset dev-tsan

# Format every tracked C/C++ and Swift source file in place.
format:
    @git ls-files -co --exclude-standard "*.c" "*.cc" "*.cpp" "*.cxx" "*.h" "*.hh" "*.hpp" "*.hxx" "*.cppm" | xargs clang-format -i
    @if command -v swift-format >/dev/null 2>&1; then git ls-files -co --exclude-standard "*.swift" | xargs swift-format format --in-place; fi

# Build the HTML documentation site into build/docs/html.
generate-docs:
    python3 tools/docs/build_docs.py

# Build/smoke Debug first, then write a whole-project clang-tidy report without changing source.
tidy-report: debug-smoke
    python3 skills/clang-tidy-report/scripts/run_clang_tidy.py --mode all --no-fix --format md --report clang_tidy_report.md

# Remove every generated build tree; source files are untouched.
clean:
    cmake -E rm -rf build
