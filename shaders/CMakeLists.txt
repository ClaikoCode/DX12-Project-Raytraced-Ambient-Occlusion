# CMake pattern for compiling shaders with properties (inspired from: https://stackoverflow.com/questions/71299716/how-to-compile-hlsl-shaders-during-build-with-cmake)

add_custom_target(Shaders)

# Set shader files
set(HLSL_VERTEX_SHADERS DeferredRenderVS.hlsl FullScreenQuadVS.hlsl)
set(HLSL_PIXEL_SHADERS DeferredRenderPS.hlsl DeferredLightingPS.hlsl AccumulationPS.hlsl)


# Set shader type properties
set_source_files_properties(${HLSL_VERTEX_SHADERS} PROPERTIES ShaderType "vs")
set_source_files_properties(${HLSL_PIXEL_SHADERS} PROPERTIES ShaderType "ps")

# Combine all shader files
set(HLSL_SHADER_FILES ${HLSL_VERTEX_SHADERS} ${HLSL_PIXEL_SHADERS})

# Set all shaders to ShaderModel 5.1
set_source_files_properties(${HLSL_SHADER_FILES} PROPERTIES ShaderModel "6_3")

# Compile all regular shaders
foreach(FILE ${HLSL_SHADER_FILES})
  get_filename_component(FILE_WE ${FILE} NAME_WE)
  get_source_file_property(shadertype ${FILE} ShaderType)
  get_source_file_property(shadermodel ${FILE} ShaderModel)
  add_custom_command(
        TARGET Shaders
        COMMAND dxc.exe /nologo /Emain /T${shadertype}_${shadermodel} $<IF:$<CONFIG:DEBUG>,/Od,/O1> /Zi /Fo ${CMAKE_BINARY_DIR}/${FILE_WE}.cso /Fd ${CMAKE_BINARY_DIR}/${FILE_WE}.pdb ${FILE}
        MAIN_DEPENDENCY ${FILE}
        COMMENT "HLSL ${FILE}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM
  )
endforeach(FILE)


set(HLSL_RAYTRACING_SHADERS RTAOShader.hlsl)

set_source_files_properties(${HLSL_RAYTRACING_SHADERS} PROPERTIES ShaderModel "6_3")

# Compile all raster shaders.
foreach(FILE ${HLSL_RAYTRACING_SHADERS})
  get_filename_component(FILE_WE ${FILE} NAME_WE)
  get_source_file_property(shadermodel ${FILE} ShaderModel)
  add_custom_command(
        TARGET Shaders
        COMMAND dxc.exe /nologo /T lib_${shadermodel} $<IF:$<CONFIG:DEBUG>,/Od,/O1> /Zi /Fo ${CMAKE_BINARY_DIR}/${FILE_WE}.dxil /Fd ${CMAKE_BINARY_DIR}/${FILE_WE}.pdb ${FILE}
        MAIN_DEPENDENCY ${FILE}
        COMMENT "HLSL ${FILE}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM
  )
endforeach(FILE)