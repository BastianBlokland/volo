# --------------------------------------------------------------------------------------------------
# Sandbox application cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: sandbox")

add_executable(app_sandbox
  src/app.c
  src/main.c
  )

target_link_libraries(app_sandbox PRIVATE lib_asset)
target_link_libraries(app_sandbox PRIVATE lib_cli)
target_link_libraries(app_sandbox PRIVATE lib_debug)
target_link_libraries(app_sandbox PRIVATE lib_gap)
target_link_libraries(app_sandbox PRIVATE lib_input)
target_link_libraries(app_sandbox PRIVATE lib_jobs)
target_link_libraries(app_sandbox PRIVATE lib_log)
target_link_libraries(app_sandbox PRIVATE lib_rend)
target_link_libraries(app_sandbox PRIVATE lib_scene)
target_link_libraries(app_sandbox PRIVATE lib_ui)
add_dependencies(app_sandbox volo_assets)
configure_debuggable(app_sandbox)

add_custom_target(run.sandbox COMMAND app_sandbox
  "--assets" "$<TARGET_PROPERTY:volo_assets,path>" VERBATIM USES_TERMINAL)
