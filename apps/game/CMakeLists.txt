# --------------------------------------------------------------------------------------------------
# Game application cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: game")

add_executable(app_game
  src/app.c
  src/cmd.c
  src/hud.c
  src/input.c
  src/prefs.c
  )

target_link_libraries(app_game PRIVATE
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
configure_debuggable(app_game)

add_custom_target(run.game
  COMMAND app_fetch "${CMAKE_SOURCE_DIR}/assets/fetch.json" VERBATIM USES_TERMINAL
  COMMAND app_game "-w" "--assets" "${CMAKE_SOURCE_DIR}/assets" VERBATIM USES_TERMINAL
)
