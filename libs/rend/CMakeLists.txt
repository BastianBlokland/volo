# --------------------------------------------------------------------------------------------------
# Renderer library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: rend")

add_library(lib_rend STATIC
  src/rvk/buffer.c
  src/rvk/canvas.c
  src/rvk/debug.c
  src/rvk/desc.c
  src/rvk/device.c
  src/rvk/graphic.c
  src/rvk/image.c
  src/rvk/mem_alloc.c
  src/rvk/mem_pool.c
  src/rvk/mesh.c
  src/rvk/pass.c
  src/rvk/renderer.c
  src/rvk/repository.c
  src/rvk/sampler.c
  src/rvk/shader.c
  src/rvk/swapchain.c
  src/rvk/texture.c
  src/rvk/transfer.c
  src/rvk/vulkan.c
  src/canvas.c
  src/color.c
  src/platform.c
  src/register.c
  src/resource.c)
target_include_directories(lib_rend PUBLIC include)
target_link_libraries(lib_rend PUBLIC lib_core)
target_link_libraries(lib_rend PUBLIC lib_ecs)
target_link_libraries(lib_rend PRIVATE lib_asset)
target_link_libraries(lib_rend PRIVATE lib_gap)
target_link_libraries(lib_rend PRIVATE lib_log)
target_link_libraries(lib_rend PRIVATE lib_scene)

find_package(Vulkan REQUIRED)
message(STATUS ">> Vulkan lib: ${Vulkan_LIBRARIES}")
message(STATUS ">> Vulkan headers: ${Vulkan_INCLUDE_DIRS}")
target_link_libraries(lib_rend PRIVATE ${Vulkan_LIBRARIES})
target_include_directories(lib_rend PRIVATE ${Vulkan_INCLUDE_DIRS})

message(STATUS "> demos: rend")
add_subdirectory(demos)
