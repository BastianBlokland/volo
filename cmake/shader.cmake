# --------------------------------------------------------------------------------------------------
# CMake shader utilities.
# --------------------------------------------------------------------------------------------------

#
# Configure a target that consists of shaders.
#
# NOTE: Shaders are build in an 'in-source' fashion, meaning shader binaries (.spv) will end up next
# to the source shaders. This greatly simplies asset path handling.
#
function(configure_shaders target)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "INCLUDE_DIR" "SOURCES")

  message(STATUS "> Configuring shaders: ${target}")

  find_package(Vulkan REQUIRED)
  message(STATUS ">> Vulkan glslc: ${Vulkan_GLSLC_EXECUTABLE}")

  foreach(source ${ARG_SOURCES})

    # Create output directory.
    # Even though the shader binaries are compiled 'in-source' (so they will not end up in the
    # binary-dir) the dep (.d) files will still be generated in the binary output directory.
    get_filename_component(binParentDir ${CMAKE_CURRENT_BINARY_DIR}/${source} DIRECTORY)
    file(MAKE_DIRECTORY ${binParentDir})

    # Create a custom command to compile the shader to spir-v using glslc.
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/${source}.spv
      DEPENDS ${source}
      DEPFILE ${source}.d
      COMMAND
        ${Vulkan_GLSLC_EXECUTABLE}
        -x glsl --target-env=vulkan1.1 --target-spv=spv1.3 -Werror -O
        -MD -MF ${source}.d
        -I ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_INCLUDE_DIR}
        -o ${CMAKE_CURRENT_SOURCE_DIR}/${source}.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/${source}
      )
    list(APPEND artifacts ${CMAKE_CURRENT_SOURCE_DIR}/${source}.spv)
  endforeach()

  add_custom_target(${target} DEPENDS ${artifacts})
endfunction(configure_shaders)
