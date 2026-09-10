# The single policy every LightPHI-owned C++ target follows.
#
# Call this instead of repeating warnings, the exception/RTTI policy, sanitizer wiring and the
# library version properties at each target. Shared libraries additionally get the project version
# and, on Windows, automatic symbol export.

include(${CMAKE_CURRENT_LIST_DIR}/Warnings.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Sanitizers.cmake)

function(lightphi_configure_target target)
  lightphi_set_warnings(${target})
  lightphi_disable_exceptions(${target})
  lightphi_apply_enabled_sanitizers(${target})

  target_compile_features(${target} PUBLIC cxx_std_23)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

  get_target_property(type ${target} TYPE)
  if(type STREQUAL "SHARED_LIBRARY")
    set_target_properties(${target} PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
    if(WIN32)
      set_target_properties(${target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
  endif()
endfunction()
