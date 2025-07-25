# --------------------------------------------------------------------------------------------------
# Utility applications cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: schemasetup")

add_executable(app_schemasetup schemasetup.c)
target_link_libraries(app_schemasetup PRIVATE lib_app_cli lib_asset lib_log)
configure_debuggable(app_schemasetup)

add_custom_target(run.schemasetup COMMAND
  app_schemasetup "${CMAKE_SOURCE_DIR}/assets/schemas/" VERBATIM USES_TERMINAL)

message(STATUS "> application: fetch")

add_executable(app_fetch fetch.c)
target_link_libraries(app_fetch PRIVATE lib_app_cli lib_net lib_log lib_data)
configure_debuggable(app_fetch)

add_custom_target(run.fetch COMMAND app_fetch
  "${CMAKE_SOURCE_DIR}/assets/fetch.json" VERBATIM USES_TERMINAL)

add_custom_target(run.fetch.force COMMAND app_fetch --force
  "${CMAKE_SOURCE_DIR}/assets/fetch.json" VERBATIM USES_TERMINAL)

message(STATUS "> application: pack")

add_executable(app_pack pack.c)
target_link_libraries(app_pack PRIVATE lib_app_ecs lib_log lib_asset)
configure_debuggable(app_pack)

add_custom_target(run.pack COMMAND app_pack
  "--assets" "${CMAKE_SOURCE_DIR}/assets"
  "--output" "${CMAKE_BINARY_DIR}/assets.blob"
  "${CMAKE_SOURCE_DIR}/assets/pack.json" VERBATIM USES_TERMINAL)

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

message(STATUS "> application: httpu")

add_executable(app_httpu httpu.c)
target_link_libraries(app_httpu PRIVATE lib_app_cli lib_net lib_log)
configure_debuggable(app_httpu)

message(STATUS "> application: vkgen")

add_executable(app_vkgen vkgen.c)
target_link_libraries(app_vkgen PRIVATE lib_app_cli lib_net lib_xml lib_log)
configure_debuggable(app_vkgen)

add_custom_target(run.vkgen COMMAND
  app_vkgen "${CMAKE_SOURCE_DIR}/libs/rend/src/rvk/vulkan_api" VERBATIM USES_TERMINAL)
