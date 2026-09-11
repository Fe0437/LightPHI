set shell := ["sh", "-cu"]

debug-build:
    cmake --preset debug
    cmake --build --preset debug

debug-test: debug-build
    ctest --preset debug

format:
    clang-format -i source/input.cppm source/backend_fake/*.cppm source/backend_fake/*.cpp tests/*.cpp tests/conformance/*.cpp
