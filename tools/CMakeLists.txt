# --------------------------------------------------------------------------------------------------
# Tools cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> tool: dbgsetup")

add_executable(volo_dbgsetup dbgsetup.c)
target_link_libraries(volo_dbgsetup PRIVATE lib_core)
target_link_libraries(volo_dbgsetup PRIVATE lib_cli)
target_link_libraries(volo_dbgsetup PRIVATE lib_json)
target_link_libraries(volo_dbgsetup PRIVATE lib_log)
