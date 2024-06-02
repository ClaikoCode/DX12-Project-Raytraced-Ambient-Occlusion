project(tinyobjloader)

add_library(tinyobjloader INTERFACE)

target_include_directories(tinyobjloader INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET tinyobjloader PROPERTY CXX_STANDARD 17)
endif()