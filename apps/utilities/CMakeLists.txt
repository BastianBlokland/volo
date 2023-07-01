# --------------------------------------------------------------------------------------------------
# Utility applications cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: schemasetup")

add_executable(app_schemasetup schemasetup.c)
target_link_libraries(app_schemasetup PRIVATE lib_app_cli lib_asset lib_log)
configure_debuggable(app_schemasetup)

add_custom_target(schemasetup COMMAND app_schemasetup
 "--out" "$<TARGET_PROPERTY:volo_assets,path>/schemas/bt.btschema" VERBATIM USES_TERMINAL)

message(STATUS "> application: dbgsetup")

add_executable(app_dbgsetup dbgsetup.c)
target_link_libraries(app_dbgsetup PRIVATE lib_app_cli lib_json lib_log)
