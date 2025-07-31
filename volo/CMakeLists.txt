# --------------------------------------------------------------------------------------------------
# Volo application cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: volo")

add_executable(volo
  src/app.c
  src/cmd.c
  src/hud.c
  src/input.c
  src/prefs.c
  )

target_link_libraries(volo PRIVATE
  lib_app_ecs
  lib_asset
  lib_data
  lib_dev
  lib_gap
  lib_input
  lib_rend
  lib_scene
  lib_snd
  lib_trace
  lib_ui
  lib_vfx
  )
configure_debuggable(volo)

install(TARGETS volo DESTINATION "${CMAKE_SOURCE_DIR}/bin")
if(${VOLO_PLATFORM} STREQUAL "win32" AND NOT ${VOLO_COMPILER} STREQUAL "gcc")
  install(FILES $<TARGET_PDB_FILE:volo> DESTINATION "${CMAKE_SOURCE_DIR}/bin")
endif()

add_custom_target(run.volo
  COMMAND fetch "${CMAKE_SOURCE_DIR}/assets/fetch.json" VERBATIM USES_TERMINAL
  COMMAND volo "-w" "--assets" "${CMAKE_SOURCE_DIR}/assets" VERBATIM USES_TERMINAL
)
