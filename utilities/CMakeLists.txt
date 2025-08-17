# --------------------------------------------------------------------------------------------------
# Utility applications cmake file.
# --------------------------------------------------------------------------------------------------

add_executable(schemasetup schemasetup.c)
target_link_libraries(schemasetup PRIVATE lib_app_cli lib_asset lib_log)
configure_debuggable(schemasetup)

add_executable(fetch fetch.c)
target_link_libraries(fetch PRIVATE lib_app_cli lib_net lib_log lib_data)
configure_debuggable(fetch)

add_executable(pack pack.c)
target_link_libraries(pack PRIVATE lib_app_ecs lib_log lib_asset)
configure_debuggable(pack)

add_executable(dbgsetup dbgsetup.c)
target_link_libraries(dbgsetup PRIVATE lib_app_cli lib_json lib_log)

add_executable(repl repl.c)
target_link_libraries(repl PRIVATE lib_app_cli lib_script)
configure_debuggable(repl)

add_executable(lsp lsp.c)
target_link_libraries(lsp PRIVATE lib_app_cli lib_script lib_json)
configure_debuggable(lsp)

add_executable(bcu bcu.c)
target_link_libraries(bcu PRIVATE lib_app_cli lib_log)
configure_debuggable(bcu)

add_executable(blob2j blob2j.c)
target_link_libraries(blob2j PRIVATE lib_app_cli lib_asset)
configure_debuggable(blob2j)

add_executable(zipu zipu.c)
target_link_libraries(zipu PRIVATE lib_app_cli lib_log)
configure_debuggable(zipu)

add_executable(httpu httpu.c)
target_link_libraries(httpu PRIVATE lib_app_cli lib_net lib_log)
configure_debuggable(httpu)

add_executable(vkgen vkgen.c)
target_link_libraries(vkgen PRIVATE lib_app_cli lib_net lib_xml lib_log)
configure_debuggable(vkgen)
