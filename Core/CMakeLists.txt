
add_executable(Core WIN32 "Main.cpp" "Window.cpp" "App.cpp" "DirectXIncludes.h" "GraphicsErrorHandling.cpp" "DX12Renderer.cpp" "DX12Renderer.h" "GPUResource.cpp" "GPUResource.h" "DX12AbstractionUtils.h" "DX12SyncHandler.h" "DX12SyncHandler.cpp" "DX12RenderPass.h" "DX12RenderPass.cpp" "AppDefines.h" "RenderObject.h" "RenderObject.cpp" "Camera.h" "Camera.cpp")

# Set debug directory to the same as the output directory for MSVC compilers.
set_property(TARGET Core PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_dependencies(Core Shaders)
target_compile_definitions(Core PRIVATE _UNICODE UNICODE _WIN32_WINNT=0x0A00 TINYOBJLOADER_IMPLEMENTATION)
target_link_libraries(Core PRIVATE DirectX-Headers tinyobjloader d3d12 dxcore dxgi d3dcompiler)



if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Core PROPERTY CXX_STANDARD 20)
endif()

