# Optional sanitizer instrumentation for LightPHI-owned C++ targets.
#
# The presets turn these on; nothing is instrumented by default. Compile options stay PRIVATE, link
# options are PUBLIC so a consumer that links an instrumented LightPHI also links its runtime.

option(LIGHTPHI_ENABLE_ASAN "Instrument LightPHI targets with AddressSanitizer" OFF)
option(LIGHTPHI_ENABLE_UBSAN "Instrument LightPHI targets with UndefinedBehaviorSanitizer" OFF)
option(LIGHTPHI_ENABLE_TSAN "Instrument LightPHI targets with ThreadSanitizer" OFF)

function(lightphi_enable_sanitizer target sanitizer)
  if(MSVC)
    if(sanitizer STREQUAL "asan")
      target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/fsanitize=address>")
    else()
      message(WARNING "[LightPHI] Sanitizer '${sanitizer}' is not supported on MSVC")
    endif()
    return()
  endif()

  set(flags "")
  if(sanitizer STREQUAL "asan")
    list(APPEND flags -fsanitize=address -fno-omit-frame-pointer)
  elseif(sanitizer STREQUAL "ubsan")
    list(APPEND flags -fsanitize=undefined -fno-omit-frame-pointer)
  elseif(sanitizer STREQUAL "tsan")
    list(APPEND flags -fsanitize=thread)
  else()
    message(FATAL_ERROR "[LightPHI] Unknown sanitizer: ${sanitizer}")
  endif()

  target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${flags}>")
  target_link_options(${target} PUBLIC ${flags})
endfunction()

function(lightphi_apply_enabled_sanitizers target)
  if(LIGHTPHI_ENABLE_ASAN)
    lightphi_enable_sanitizer(${target} asan)
  endif()
  if(LIGHTPHI_ENABLE_UBSAN)
    lightphi_enable_sanitizer(${target} ubsan)
  endif()
  if(LIGHTPHI_ENABLE_TSAN)
    lightphi_enable_sanitizer(${target} tsan)
  endif()
endfunction()
