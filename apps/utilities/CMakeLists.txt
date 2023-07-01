# --------------------------------------------------------------------------------------------------
# Utility applications cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: schemesetup")

add_executable(app_schemesetup schemesetup.c)
target_link_libraries(app_schemesetup PRIVATE lib_app_cli lib_asset lib_log)
configure_debuggable(app_schemesetup)

add_custom_target(schemesetup COMMAND app_schemesetup
 "--out" "$<TARGET_PROPERTY:volo_assets,path>/behaviors/ai.btschema" VERBATIM USES_TERMINAL)

message(STATUS "> application: dbgsetup")

add_executable(app_dbgsetup dbgsetup.c)
target_link_libraries(app_dbgsetup PRIVATE lib_app_cli lib_json lib_log)
