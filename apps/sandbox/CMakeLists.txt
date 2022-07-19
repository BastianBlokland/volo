# --------------------------------------------------------------------------------------------------
# Sandbox application cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: sandbox")

add_executable(app_sandbox
  src/app.c
  )

target_link_libraries(app_sandbox PRIVATE
  lib_app_cli
  lib_asset
  lib_debug
  lib_gap
  lib_input
  lib_jobs
  lib_log
  lib_rend
  lib_scene
  lib_ui
  )
add_dependencies(app_sandbox volo_assets)
configure_debuggable(app_sandbox)

add_custom_target(run.sandbox COMMAND app_sandbox
  "--assets" "$<TARGET_PROPERTY:volo_assets,path>" VERBATIM USES_TERMINAL)
