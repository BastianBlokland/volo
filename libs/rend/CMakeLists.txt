# --------------------------------------------------------------------------------------------------
# Renderer library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: rend")

add_library(lib_rend STATIC
  src/draw.c
  src/instance.c
  src/light.c
  src/limiter.c
  src/painter.c
  src/platform.c
  src/register.c
  src/reset.c
  src/resource.c
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
  src/rvk/psocache.c
  src/rvk/renderer.c
  src/rvk/repository.c
  src/rvk/sampler.c
  src/rvk/shader.c
  src/rvk/statrecorder.c
  src/rvk/stopwatch.c
  src/rvk/swapchain.c
  src/rvk/texture.c
  src/rvk/transfer.c
  src/rvk/types.c
  src/rvk/uniform.c
  src/rvk/vulkan.c
  src/settings.c
  src/stats.c
  src/terrain.c
  src/view.c
  )
target_include_directories(lib_rend PUBLIC include)
target_link_libraries(lib_rend PUBLIC lib_core lib_ecs lib_scene)
target_link_libraries(lib_rend PRIVATE lib_asset lib_gap lib_log)

find_package(Vulkan REQUIRED)
message(STATUS ">> Vulkan lib: ${Vulkan_LIBRARIES}")
message(STATUS ">> Vulkan headers: ${Vulkan_INCLUDE_DIRS}")
target_link_libraries(lib_rend PRIVATE ${Vulkan_LIBRARIES})
target_include_directories(lib_rend PRIVATE ${Vulkan_INCLUDE_DIRS})
