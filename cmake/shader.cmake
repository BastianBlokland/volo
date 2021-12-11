# --------------------------------------------------------------------------------------------------
# CMake shader utilities.
# --------------------------------------------------------------------------------------------------

#
# Configure a target that consists of shaders.
#
function(configure_shaders target)
  message(STATUS "> Configuring shaders: ${target}")

  find_package(Vulkan REQUIRED)
  message(STATUS ">> Vulkan glslc: ${Vulkan_GLSLC_EXECUTABLE}")

  foreach(shaderPath IN LISTS ARGN)
    # Create a custom command to compile the shader to spir-v using glslc.
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${shaderPath}.spv
      DEPENDS ${shaderPath}
      COMMAND ${Vulkan_GLSLC_EXECUTABLE}
      --target-env=vulkan1.1 --target-spv=spv1.3 -Werror -O
      -o ${CMAKE_CURRENT_BINARY_DIR}/${shaderPath}.spv ${CMAKE_CURRENT_SOURCE_DIR}/${shaderPath})

    list(APPEND artifacts ${CMAKE_CURRENT_BINARY_DIR}/${shaderPath}.spv)
  endforeach()

  add_custom_target(${target} DEPENDS ${artifacts})

endfunction(configure_shaders)
