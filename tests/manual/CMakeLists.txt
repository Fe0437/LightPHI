# Checks that need a person and a real device. Built only on request and never registered with
# CTest; docs/MANUAL_CHECKS.md says when and how to run them.

if(NOT TARGET LightPHIMacOSCapture)
  return()
endif()

add_library(lightphi_pen_check_window STATIC PenCheckWindow.swift ../support/AppKitEventFixture.swift)
target_compile_options(lightphi_pen_check_window PRIVATE ${LIGHTPHI_SWIFT_INTEROP_OPTIONS})
target_link_libraries(lightphi_pen_check_window PUBLIC LightPHIMacOSCapture Microsoft.GSL::GSL "-framework AppKit")

add_executable(lightphi_pen_check pen_check.cpp)
target_link_libraries(lightphi_pen_check PRIVATE LightPHI::Backend lightphi_pen_check_window)
lightphi_configure_target(lightphi_pen_check)
