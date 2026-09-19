# Native DXIL is sufficient for the single D3D12 spike. See ADR 0002.
set(WHITE_DXC_ROOT "${CMAKE_SOURCE_DIR}/tools/dxc" CACHE PATH "Extracted DXC v1.8.2505.1 directory")
find_program(WHITE_DXC NAMES dxc.exe PATHS "${WHITE_DXC_ROOT}/bin/x64" NO_DEFAULT_PATH REQUIRED)
function(white_compile_shader filename profile)
  set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${filename}.dxil")
  add_custom_command(OUTPUT "${output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${WHITE_DXC}" -T ${profile} -E main -Ges -WX -O3
      "${CMAKE_SOURCE_DIR}/shaders/${filename}" -Fo "${output}"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/${filename}" "${CMAKE_SOURCE_DIR}/shaders/density.hlsli" VERBATIM)
  string(REPLACE "." "_" target_name "${filename}")
  add_custom_target(shader_${target_name} DEPENDS "${output}")
  add_dependencies(white_app shader_${target_name})
  add_custom_command(TARGET white_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:white_app>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${output}" "$<TARGET_FILE_DIR:white_app>/shaders/")
endfunction()
