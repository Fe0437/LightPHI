cmake_minimum_required(VERSION 3.28)

project(LightPHI VERSION 1.0.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)
include(cmake/Targets.cmake)

if(NOT TARGET Microsoft.GSL::GSL)
  set(GSL_INSTALL OFF CACHE BOOL "" FORCE)
  set(GSL_TEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    MicrosoftGSL
    GIT_REPOSITORY https://github.com/microsoft/GSL.git
    GIT_TAG v4.2.2
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(MicrosoftGSL)
endif()

set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_LIGHTPHI_STANDALONE ON)
else()
  set(_LIGHTPHI_STANDALONE OFF)
endif()

option(LIGHTPHI_BUILD_FAKE_BACKEND "Build the deterministic fake input backend" ${_LIGHTPHI_STANDALONE})
option(LIGHTPHI_BUILD_TESTS "Build the LightPHI conformance and API/ABI smoke tests" ${_LIGHTPHI_STANDALONE})
option(LIGHTPHI_BUILD_EXAMPLES "Build the LightPHI API examples" ${_LIGHTPHI_STANDALONE})

add_library(LightPHI SHARED)
add_library(LightPHI::LightPHI ALIAS LightPHI)

file(GLOB _lightphi_modules CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/phi/*.cppm")
target_sources(
  LightPHI
  PUBLIC
  FILE_SET CXX_MODULES
  BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/source"
  FILES ${_lightphi_modules})
lightphi_configure_target(LightPHI)

if(LIGHTPHI_BUILD_FAKE_BACKEND)
  file(GLOB _lightphi_fake_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/backend_fake/*.cpp")
  file(GLOB _lightphi_fake_modules CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/backend_fake/*.cppm")
  add_library(LightPHIFakeBackend SHARED ${_lightphi_fake_sources})
  add_library(LightPHI::FakeBackend ALIAS LightPHIFakeBackend)
  target_sources(
    LightPHIFakeBackend
    PUBLIC FILE_SET CXX_MODULES FILES ${_lightphi_fake_modules})
  target_link_libraries(LightPHIFakeBackend PUBLIC LightPHI::LightPHI)
  target_link_libraries(LightPHIFakeBackend PRIVATE Microsoft.GSL::GSL)
  lightphi_configure_target(LightPHIFakeBackend)
endif()

if(APPLE)
  file(GLOB _lightphi_apple_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/backend_apple/*.cpp")
  add_library(LightPHIAppleInput STATIC ${_lightphi_apple_sources})
  target_include_directories(LightPHIAppleInput PUBLIC source/backend_apple)
  target_link_libraries(LightPHIAppleInput PUBLIC Microsoft.GSL::GSL)
  lightphi_configure_target(LightPHIAppleInput)
endif()

if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  enable_language(Swift)
  set(LIGHTPHI_BACKEND "macOS" CACHE INTERNAL "Active LightPHI backend")
  message(STATUS "[LightPHI] Backend: ${LIGHTPHI_BACKEND}")

  execute_process(
    COMMAND xcrun swiftc -print-target-info
    OUTPUT_VARIABLE _lightphi_swift_target_info
    COMMAND_ERROR_IS_FATAL ANY)
  string(JSON _lightphi_swift_resource_path GET "${_lightphi_swift_target_info}" paths runtimeResourcePath)

  set(_lightphi_swift_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
  set(_lightphi_swift_header "${_lightphi_swift_generated_dir}/LightPHIMacOSCapture-Swift.h")
  file(MAKE_DIRECTORY "${_lightphi_swift_generated_dir}")

  file(GLOB _lightphi_capture_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/backend_macos/*.swift")
  add_library(LightPHIMacOSCapture STATIC ${_lightphi_capture_sources})
  set_target_properties(
    LightPHIMacOSCapture
    PROPERTIES Swift_MODULE_NAME LightPHIMacOSCapture
               Swift_MODULE_DIRECTORY "${_lightphi_swift_generated_dir}")
  target_compile_options(
    LightPHIMacOSCapture
    PRIVATE -cxx-interoperability-mode=default
            -emit-objc-header-path
            "${_lightphi_swift_header}"
            "SHELL:-Xcc -fmodule-map-file=${CMAKE_CURRENT_SOURCE_DIR}/source/backend_apple/include/module.modulemap"
            "SHELL:-Xcc -I${CMAKE_CURRENT_SOURCE_DIR}/source/backend_apple")
  target_link_libraries(LightPHIMacOSCapture PRIVATE Microsoft.GSL::GSL "-framework AppKit")

  file(GLOB _lightphi_backend_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/backend_macos/*.cpp")
  file(GLOB _lightphi_backend_modules CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/backend_macos/*.cppm")
  add_library(LightPHIBackend SHARED ${_lightphi_backend_sources})
  add_library(LightPHI::Backend ALIAS LightPHIBackend)
  add_dependencies(LightPHIBackend LightPHIMacOSCapture)
  set_source_files_properties(
    ${_lightphi_backend_sources}
    PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}LightPHIMacOSCapture${CMAKE_STATIC_LIBRARY_SUFFIX}")
  target_sources(
    LightPHIBackend
    PUBLIC FILE_SET CXX_MODULES FILES ${_lightphi_backend_modules})
  target_include_directories(
    LightPHIBackend
    PRIVATE source/backend_apple)
  target_include_directories(
    LightPHIBackend
    SYSTEM PRIVATE "${_lightphi_swift_generated_dir}" "${_lightphi_swift_resource_path}")
  target_link_libraries(
    LightPHIBackend
    PUBLIC LightPHI::LightPHI
    PRIVATE LightPHIAppleInput LightPHIMacOSCapture Microsoft.GSL::GSL "-framework AppKit")
  lightphi_configure_target(LightPHIBackend)
endif()

if(LIGHTPHI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(LIGHTPHI_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
