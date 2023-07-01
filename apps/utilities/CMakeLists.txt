# --------------------------------------------------------------------------------------------------
# Utility applications cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: btschema")

add_executable(app_btschema btschema.c)
target_link_libraries(app_btschema PRIVATE lib_app_cli lib_asset lib_log)
configure_debuggable(app_btschema)

add_custom_target(btschema COMMAND app_btschema
 "--out" "$<TARGET_PROPERTY:volo_assets,path>/behaviors/ai.btschema" VERBATIM USES_TERMINAL)

message(STATUS "> application: dbgsetup")

add_executable(app_dbgsetup dbgsetup.c)
target_link_libraries(app_dbgsetup PRIVATE lib_app_cli lib_json lib_log)
