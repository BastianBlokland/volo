# --------------------------------------------------------------------------------------------------
# Utility applications cmake file.
# --------------------------------------------------------------------------------------------------

add_executable(schemagen schemagen.c)
target_link_libraries(schemagen PRIVATE app_cli asset log)

add_executable(fetch fetch.c)
target_link_libraries(fetch PRIVATE app_cli net log data)

add_executable(pack pack.c)
target_link_libraries(pack PRIVATE app_ecs log asset)

add_executable(dbggen dbggen.c)
target_link_libraries(dbggen PRIVATE app_cli json log)

add_executable(repl repl.c)
target_link_libraries(repl PRIVATE app_cli script)

add_executable(lsp lsp.c)
target_link_libraries(lsp PRIVATE app_cli script json)

add_executable(bcu bcu.c)
target_link_libraries(bcu PRIVATE app_cli log)

add_executable(blob2j blob2j.c)
target_link_libraries(blob2j PRIVATE app_cli asset)

add_executable(zipu zipu.c)
target_link_libraries(zipu PRIVATE app_cli log)

add_executable(httpu httpu.c)
target_link_libraries(httpu PRIVATE app_cli net log)

add_executable(vkgen vkgen.c)
target_link_libraries(vkgen PRIVATE app_cli net xml log)
