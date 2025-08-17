# --------------------------------------------------------------------------------------------------
# Volo application cmake file.
# --------------------------------------------------------------------------------------------------

add_executable(volo
  src/app.c
  src/cmd.c
  src/hud.c
  src/input.c
  src/prefs.c
  )

if(${VOLO_PLATFORM} STREQUAL "win32")
  configure_file("res/app.rc.in" "${CMAKE_CURRENT_LIST_DIR}/res/app.rc")
  target_sources(volo PRIVATE "res/app.rc")
endif()

target_link_libraries(volo PRIVATE app_ecs asset data dev gap input rend scene snd trace ui vfx)
configure_debuggable(volo)
