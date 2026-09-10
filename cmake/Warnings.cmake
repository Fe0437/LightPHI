# Warning and language-runtime policy for LightPHI-owned C++ targets.
#
# Every option is guarded by COMPILE_LANGUAGE:CXX so the Swift adapter target is unaffected.

option(LIGHTPHI_WARNINGS_AS_ERRORS "Treat LightPHI compiler warnings as errors" ON)

function(lightphi_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive->")
    if(LIGHTPHI_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/WX>")
    endif()
  else()
    target_compile_options(
      ${target}
      PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wshadow;-Wnon-virtual-dtor;-Wcast-align;-Wunused;-Woverloaded-virtual;-Wconversion;-Wsign-conversion;-Wmisleading-indentation;-Wnull-dereference;-Wdouble-promotion;-Wformat=2;-Wno-missing-field-initializers>")
    if(LIGHTPHI_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Werror>")
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options(
        ${target}
        PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-Wduplicated-cond;-Wduplicated-branches;-Wlogical-op>")
    endif()
  endif()
endfunction()

# A recoverable failure is carried by std::expected, never by unwinding, and a closed choice is a
# tagged value rather than a question asked of an object, so both language features are removed
# rather than merely discouraged.
#
# The options are PUBLIC because a module file records them: a consumer importing a LightPHI module
# must be compiled the same way or the module cannot be loaded. A consumer that needs either feature
# needs it behind a boundary that does not import these modules.
function(lightphi_disable_exceptions target)
  if(MSVC)
    target_compile_options(${target} PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:/EHs-c-;/GR->")
  else()
    target_compile_options(${target} PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")
  endif()
endfunction()
