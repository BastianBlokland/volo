# --------------------------------------------------------------------------------------------------
# Utility applications cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: btscheme")

add_executable(app_btscheme btscheme.c)
target_link_libraries(app_btscheme PRIVATE lib_app_cli lib_asset lib_log)
configure_debuggable(app_btscheme)

add_custom_target(btscheme COMMAND app_btscheme
 "--out" "$<TARGET_PROPERTY:volo_assets,path>/behaviors/ai.btscheme" VERBATIM USES_TERMINAL)

message(STATUS "> application: dbgsetup")

add_executable(app_dbgsetup dbgsetup.c)
target_link_libraries(app_dbgsetup PRIVATE lib_app_cli lib_json lib_log)
