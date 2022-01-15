# --------------------------------------------------------------------------------------------------
# Assets cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> assets")

configure_shaders(volo_shaders
  shaders/grid.vert
  shaders/grid.frag
  shaders/sky.vert
  shaders/sky.frag
  shaders/standard.vert
  shaders/standard.frag
  shaders/ui_lines.vert
  shaders/ui_lines.frag)

function(download_external_assets)
  set(remoteUrl "https://www.bastian.tech/assets")
  foreach(filePath IN LISTS ARGN)
    set(targetPath ${CMAKE_CURRENT_SOURCE_DIR}/external/${filePath})
    if(NOT EXISTS ${targetPath})
      message(STATUS ">> Downloading: ${remoteUrl}/${filePath}")
      file(DOWNLOAD "${remoteUrl}/${filePath}" ${targetPath})
    endif()
  endforeach()
endfunction(download_external_assets)

if(${EXTERNAL_ASSETS})
  download_external_assets(
    bunny.obj
    head/head.obj
    head/lambertian.tga
    corset/corset.obj
    corset/corset_diff.tga
    corset/corset_nrm.tga
    cayo/cayo.obj
    cayo/cayo_diff.tga
    cayo/cayo_nrm.tga
    pedestal/pedestal.obj
    pedestal/pedestal_diff.tga
    pedestal/pedestal_nrm.tga
    testimages/ducks.tga)
endif()

add_custom_target(volo_assets)
set_target_properties(volo_assets PROPERTIES path ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(volo_assets volo_shaders)
