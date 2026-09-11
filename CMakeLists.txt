cmake_minimum_required(VERSION 3.28)

project(LightPHI VERSION 1.0.0 LANGUAGES CXX)

include(CTest)
include(cmake/Warnings.cmake)

set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_LIGHTPHI_STANDALONE ON)
else()
  set(_LIGHTPHI_STANDALONE OFF)
endif()

option(LIGHTPHI_BUILD_FAKE_BACKEND "Build the deterministic fake input backend" ${_LIGHTPHI_STANDALONE})
option(LIGHTPHI_BUILD_TESTS "Build the LightPHI conformance and ABI tests" ${_LIGHTPHI_STANDALONE})

add_library(LightPHI SHARED)
add_library(LightPHI::LightPHI ALIAS LightPHI)
target_compile_features(LightPHI PUBLIC cxx_std_23)
target_sources(
  LightPHI
  PUBLIC FILE_SET CXX_MODULES FILES source/input.cppm)
lightphi_set_warnings(LightPHI)
lightphi_disable_exceptions(LightPHI)
set_target_properties(LightPHI PROPERTIES CXX_EXTENSIONS OFF VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
if(WIN32)
  set_target_properties(LightPHI PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if(LIGHTPHI_BUILD_FAKE_BACKEND)
  add_library(LightPHIFakeBackend SHARED source/backend_fake/fake_backend.cpp)
  add_library(LightPHI::FakeBackend ALIAS LightPHIFakeBackend)
  target_compile_features(LightPHIFakeBackend PUBLIC cxx_std_23)
  target_sources(
    LightPHIFakeBackend
    PUBLIC FILE_SET CXX_MODULES FILES source/backend_fake/fake_backend.cppm)
  target_link_libraries(LightPHIFakeBackend PUBLIC LightPHI::LightPHI)
  lightphi_set_warnings(LightPHIFakeBackend)
  lightphi_disable_exceptions(LightPHIFakeBackend)
  set_target_properties(
    LightPHIFakeBackend
    PROPERTIES CXX_EXTENSIONS OFF
               VERSION ${PROJECT_VERSION}
               SOVERSION ${PROJECT_VERSION_MAJOR})
  if(WIN32)
    set_target_properties(LightPHIFakeBackend PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  endif()
endif()

if(LIGHTPHI_BUILD_TESTS)
  if(NOT LIGHTPHI_BUILD_FAKE_BACKEND)
    message(FATAL_ERROR "LIGHTPHI_BUILD_TESTS requires LIGHTPHI_BUILD_FAKE_BACKEND")
  endif()
  enable_testing()
  add_subdirectory(tests)
endif()
