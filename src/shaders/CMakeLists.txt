cmake_minimum_required(VERSION 3.31)
project(WGSLToSpirv LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
set(WGSL_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/ColorShader.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/CompositorShader.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/ShadowShader.wgsl"
  "${CMAKE_CURRENT_LIST_DIR}/TransformShader.wgsl"
)
set(SSG_WGSL_SOURCES "${WGSL_SOURCES}" PARENT_SCOPE)

if(NOT EMSCRIPTEN)
  if(TARGET tint_cmd_tint_cmd)
    set(TINT_TARGET tint_cmd_tint_cmd)
  elseif(TARGET tint)
    set(TINT_TARGET tint)
  else()
    message(FATAL_ERROR "Expected Dawn Tint target 'tint_cmd_tint_cmd' or 'tint' to exist")
  endif()

  set(TINT_EXE "$<TARGET_FILE:${TINT_TARGET}>")
endif()

set(_validated_dir "${CMAKE_BINARY_DIR}/$<CONFIG>/shaders")

# ---------------------------------------------------------------------------
# Build rules
# ---------------------------------------------------------------------------
set(_validation_outputs)
if(NOT EMSCRIPTEN)
  foreach(_wgsl IN LISTS WGSL_SOURCES)
    get_filename_component(_name "${_wgsl}" NAME)
    set(_wgsl_validate_out "${_validated_dir}/${_name}.validated.wgsl")

    add_custom_command(
      OUTPUT "${_wgsl_validate_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_validated_dir}"
      COMMAND "${TINT_EXE}"
              --validate true
              --format wgsl
              --output-name "${_wgsl_validate_out}"
              "${_wgsl}"
      DEPENDS ${TINT_TARGET} "${_wgsl}"
      VERBATIM
    )
    list(APPEND _validation_outputs "${_wgsl_validate_out}")
  endforeach()
endif()

add_custom_target(shaders ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCE_ROOT}/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${WGSL_SOURCES} "${RESOURCE_ROOT}/shaders"
  DEPENDS ${_validation_outputs}
  VERBATIM
)
if(NOT EMSCRIPTEN)
  add_dependencies(shaders ${TINT_TARGET})
endif()
set_target_properties(shaders PROPERTIES FOLDER "Shaders")
