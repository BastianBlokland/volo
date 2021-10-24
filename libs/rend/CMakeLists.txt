# --------------------------------------------------------------------------------------------------
# Renderer library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: rend")

add_library(volo_rend STATIC
  src/vulkan/alloc_host.c
  src/vulkan/context.c
  src/vulkan/vulkan.c
  src/canvas.c
  src/platform.c
  src/register.c)
target_include_directories(volo_rend PUBLIC include)
target_link_libraries(volo_rend PUBLIC volo_core)
target_link_libraries(volo_rend PUBLIC volo_ecs)
target_link_libraries(volo_rend PRIVATE volo_gap)
target_link_libraries(volo_rend PRIVATE volo_log)

message(STATUS "> Configuring vulkan sdk")
find_package(Vulkan REQUIRED)
message(STATUS ">> Vulkan lib: ${Vulkan_LIBRARIES}")
message(STATUS ">> Vulkan headers: ${Vulkan_INCLUDE_DIRS}")
message(STATUS ">> Vulkan glslc: ${Vulkan_GLSLC_EXECUTABLE}")
target_link_libraries(volo_rend PRIVATE ${Vulkan_LIBRARIES})
target_include_directories(volo_rend PRIVATE ${Vulkan_INCLUDE_DIRS})

message(STATUS "> demos: rend")
add_subdirectory(demos)
