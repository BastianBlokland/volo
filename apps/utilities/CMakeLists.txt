# --------------------------------------------------------------------------------------------------
# Utility applications cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: schemasetup")

add_executable(app_schemasetup schemasetup.c)
target_link_libraries(app_schemasetup PRIVATE lib_app_cli lib_asset lib_log)
configure_debuggable(app_schemasetup)

add_custom_target(run.schemasetup COMMAND app_schemasetup "--out"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/arraytex.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/atlas.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/decal.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/fonttex.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/graphic.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/icon.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/inputs.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/level.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/prefabs.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/procmesh.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/proctex.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/products.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/script_binder.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/terrain.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/vfx.schema.json"
  "$<TARGET_PROPERTY:volo_assets,path>/schemas/weapons.schema.json"
  VERBATIM USES_TERMINAL)

message(STATUS "> application: dbgsetup")

add_executable(app_dbgsetup dbgsetup.c)
target_link_libraries(app_dbgsetup PRIVATE lib_app_cli lib_json lib_log)

message(STATUS "> application: repl")

add_executable(app_repl repl.c)
target_link_libraries(app_repl PRIVATE lib_app_cli lib_script)
configure_debuggable(app_repl)

add_custom_target(run.repl COMMAND app_repl VERBATIM USES_TERMINAL)

message(STATUS "> application: lsp")

add_executable(app_lsp lsp.c)
target_link_libraries(app_lsp PRIVATE lib_app_cli lib_script lib_json)
configure_debuggable(app_lsp)

message(STATUS "> application: bcu")

add_executable(app_bcu bcu.c)
target_link_libraries(app_bcu PRIVATE lib_app_cli lib_log)
configure_debuggable(app_bcu)

message(STATUS "> application: blob2j")

add_executable(app_blob2j blob2j.c)
target_link_libraries(app_blob2j PRIVATE lib_app_cli lib_asset)
configure_debuggable(app_blob2j)

message(STATUS "> application: zipu")

add_executable(app_zipu zipu.c)
target_link_libraries(app_zipu PRIVATE lib_app_cli lib_log)
configure_debuggable(app_zipu)
