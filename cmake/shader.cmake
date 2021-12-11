# --------------------------------------------------------------------------------------------------
# CMake shader utilities.
# --------------------------------------------------------------------------------------------------

#
# Configure a set of shaders that are part of the given target.
#
function(configure_shaders target)
  message(STATUS "> Configuring shaders: ${target}")

  find_package(Vulkan REQUIRED)
  message(STATUS ">> Vulkan glslc: ${Vulkan_GLSLC_EXECUTABLE}")

  foreach(shaderPath IN LISTS ARGN)
    get_filename_component(shaderName ${shaderPath} NAME)

    # Create a custom command to compile the shader to spir-v using glslc.
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${shaderPath}.spv
      DEPENDS ${shaderPath}
      COMMAND ${Vulkan_GLSLC_EXECUTABLE}
      --target-env=vulkan1.1 --target-spv=spv1.3 -Werror -O
      -o ${CMAKE_CURRENT_BINARY_DIR}/${shaderPath}.spv ${CMAKE_CURRENT_SOURCE_DIR}/${shaderPath})

    # Add a convenience target that will recompile the shader if changed.
    add_custom_target("shader_${shaderName}" DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${shaderPath}.spv)

    # Add a dependency from the given target to the shader to be compiled.
    add_dependencies(${target} "shader_${shaderName}")
  endforeach()

endfunction(configure_shaders)
