# --------------------------------------------------------------------------------------------------
# Utility applications cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: schemasetup")

add_executable(schemasetup schemasetup.c)
target_link_libraries(schemasetup PRIVATE lib_app_cli lib_asset lib_log)
configure_debuggable(schemasetup)

add_custom_target(run.schemasetup COMMAND
  schemasetup "${CMAKE_SOURCE_DIR}/assets/schemas/" VERBATIM USES_TERMINAL)

message(STATUS "> application: fetch")

add_executable(fetch fetch.c)
target_link_libraries(fetch PRIVATE lib_app_cli lib_net lib_log lib_data)
configure_debuggable(fetch)

install(TARGETS fetch DESTINATION "${CMAKE_SOURCE_DIR}/bin")
if(${VOLO_PLATFORM} STREQUAL "win32" AND NOT ${VOLO_COMPILER} STREQUAL "gcc")
  install(FILES $<TARGET_PDB_FILE:fetch> DESTINATION "${CMAKE_SOURCE_DIR}/bin")
endif()

add_custom_target(run.fetch COMMAND fetch
  "${CMAKE_SOURCE_DIR}/assets/fetch.json" VERBATIM USES_TERMINAL)

add_custom_target(run.fetch.force COMMAND fetch --force
  "${CMAKE_SOURCE_DIR}/assets/fetch.json" VERBATIM USES_TERMINAL)

message(STATUS "> application: pack")

add_executable(pack pack.c)
target_link_libraries(pack PRIVATE lib_app_ecs lib_log lib_asset)
configure_debuggable(pack)

install(TARGETS pack DESTINATION "${CMAKE_SOURCE_DIR}/bin")
if(${VOLO_PLATFORM} STREQUAL "win32" AND NOT ${VOLO_COMPILER} STREQUAL "gcc")
  install(FILES $<TARGET_PDB_FILE:pack> DESTINATION "${CMAKE_SOURCE_DIR}/bin")
endif()

add_custom_target(run.pack
  COMMAND fetch "${CMAKE_SOURCE_DIR}/assets/fetch.json" VERBATIM USES_TERMINAL
  COMMAND pack
    "--assets" "${CMAKE_SOURCE_DIR}/assets"
    "--output" "${CMAKE_BINARY_DIR}/assets.blob"
    "${CMAKE_SOURCE_DIR}/assets/pack.json"
    VERBATIM USES_TERMINAL)

message(STATUS "> application: dbgsetup")

add_executable(dbgsetup dbgsetup.c)
target_link_libraries(dbgsetup PRIVATE lib_app_cli lib_json lib_log)

message(STATUS "> application: repl")

add_executable(repl repl.c)
target_link_libraries(repl PRIVATE lib_app_cli lib_script)
configure_debuggable(repl)

install(TARGETS repl DESTINATION "${CMAKE_SOURCE_DIR}/bin")
if(${VOLO_PLATFORM} STREQUAL "win32" AND NOT ${VOLO_COMPILER} STREQUAL "gcc")
  install(FILES $<TARGET_PDB_FILE:repl> DESTINATION "${CMAKE_SOURCE_DIR}/bin")
endif()

add_custom_target(run.repl COMMAND repl VERBATIM USES_TERMINAL)

message(STATUS "> application: lsp")

add_executable(lsp lsp.c)
target_link_libraries(lsp PRIVATE lib_app_cli lib_script lib_json)
configure_debuggable(lsp)

install(TARGETS lsp DESTINATION "${CMAKE_SOURCE_DIR}/bin")
if(${VOLO_PLATFORM} STREQUAL "win32" AND NOT ${VOLO_COMPILER} STREQUAL "gcc")
  install(FILES $<TARGET_PDB_FILE:lsp> DESTINATION "${CMAKE_SOURCE_DIR}/bin")
endif()

message(STATUS "> application: bcu")

add_executable(bcu bcu.c)
target_link_libraries(bcu PRIVATE lib_app_cli lib_log)
configure_debuggable(bcu)

message(STATUS "> application: blob2j")

add_executable(blob2j blob2j.c)
target_link_libraries(blob2j PRIVATE lib_app_cli lib_asset)
configure_debuggable(blob2j)

install(TARGETS blob2j DESTINATION "${CMAKE_SOURCE_DIR}/bin")
if(${VOLO_PLATFORM} STREQUAL "win32" AND NOT ${VOLO_COMPILER} STREQUAL "gcc")
  install(FILES $<TARGET_PDB_FILE:blob2j> DESTINATION "${CMAKE_SOURCE_DIR}/bin")
endif()

message(STATUS "> application: zipu")

add_executable(zipu zipu.c)
target_link_libraries(zipu PRIVATE lib_app_cli lib_log)
configure_debuggable(zipu)

message(STATUS "> application: httpu")

add_executable(httpu httpu.c)
target_link_libraries(httpu PRIVATE lib_app_cli lib_net lib_log)
configure_debuggable(httpu)

message(STATUS "> application: vkgen")

add_executable(vkgen vkgen.c)
target_link_libraries(vkgen PRIVATE lib_app_cli lib_net lib_xml lib_log)
configure_debuggable(vkgen)

add_custom_target(run.vkgen COMMAND
  vkgen "${CMAKE_SOURCE_DIR}/libs/rend/src/rvk/vulkan_api" VERBATIM USES_TERMINAL)
