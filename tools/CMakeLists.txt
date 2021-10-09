# --------------------------------------------------------------------------------------------------
# Tools cmake file.
# --------------------------------------------------------------------------------------------------

add_executable(volo_dbgsetup dbgsetup.c)
target_link_libraries(volo_dbgsetup PRIVATE volo_core)
target_link_libraries(volo_dbgsetup PRIVATE volo_cli)
target_link_libraries(volo_dbgsetup PRIVATE volo_json)
target_link_libraries(volo_dbgsetup PRIVATE volo_log)
