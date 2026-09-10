# Each example is guarded by the target it actually needs. Only selected_source requires a platform
# backend, so a platform without one still builds and runs the rest.

add_executable(lightphi_validate_sample validate_sample.cpp)
target_link_libraries(lightphi_validate_sample PRIVATE LightPHI::LightPHI)
lightphi_configure_target(lightphi_validate_sample)

if(TARGET LightPHI::FakeBackend)
  add_executable(lightphi_scripted_input scripted_input.cpp)
  target_link_libraries(lightphi_scripted_input PRIVATE LightPHI::FakeBackend)
  lightphi_configure_target(lightphi_scripted_input)
endif()

if(TARGET LightPHI::Backend)
  add_executable(lightphi_selected_source selected_source.cpp)
  target_link_libraries(lightphi_selected_source PRIVATE LightPHI::Backend)
  lightphi_configure_target(lightphi_selected_source)
endif()
