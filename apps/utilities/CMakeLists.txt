# --------------------------------------------------------------------------------------------------
# Utility applications cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: schemasetup")

add_executable(app_schemasetup schemasetup.c)
target_link_libraries(app_schemasetup PRIVATE lib_app_cli lib_asset lib_log)
configure_debuggable(app_schemasetup)

add_custom_target(run.schemasetup COMMAND app_schemasetup "--out"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/atl.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/atx.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/bt.btschema"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/bt.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/dcl.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/ftx.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/graphic.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/imp.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/level.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/pfb.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/pme.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/pro.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/ptx.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/vfx.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/wea.schema.json"
  VERBATIM USES_TERMINAL)

message(STATUS "> application: dbgsetup")

add_executable(app_dbgsetup dbgsetup.c)
target_link_libraries(app_dbgsetup PRIVATE lib_app_cli lib_json lib_log)

message(STATUS "> application: repl")

add_executable(app_repl repl.c)
target_link_libraries(app_repl PRIVATE lib_app_cli lib_script)
configure_debuggable(app_repl)

add_custom_target(run.repl COMMAND app_repl VERBATIM USES_TERMINAL)
