# --------------------------------------------------------------------------------------------------
# Applications cmake file.
# --------------------------------------------------------------------------------------------------

#
# Pedestal app.
#
add_executable(app_pedestal pedestal.c)
target_link_libraries(app_pedestal PRIVATE lib_rend)
target_link_libraries(app_pedestal PRIVATE lib_asset)
target_link_libraries(app_pedestal PRIVATE lib_cli)
target_link_libraries(app_pedestal PRIVATE lib_gap)
target_link_libraries(app_pedestal PRIVATE lib_jobs)
target_link_libraries(app_pedestal PRIVATE lib_log)
target_link_libraries(app_pedestal PRIVATE lib_scene)
add_dependencies(app_pedestal volo_assets)
configure_debuggable(app_pedestal)

#
# Font app.
#
add_executable(app_font font.c)
target_link_libraries(app_font PRIVATE lib_rend)
target_link_libraries(app_font PRIVATE lib_asset)
target_link_libraries(app_font PRIVATE lib_cli)
target_link_libraries(app_font PRIVATE lib_gap)
target_link_libraries(app_font PRIVATE lib_jobs)
target_link_libraries(app_font PRIVATE lib_log)
target_link_libraries(app_font PRIVATE lib_scene)
add_dependencies(app_font volo_assets)
configure_debuggable(app_font)

#
# Convienience run targets.
#
add_custom_target(run.pedestal COMMAND app_pedestal
  "--assets" "$<TARGET_PROPERTY:volo_assets,path>" VERBATIM USES_TERMINAL)
add_custom_target(run.font COMMAND app_font
  "--assets" "$<TARGET_PROPERTY:volo_assets,path>" VERBATIM USES_TERMINAL)
