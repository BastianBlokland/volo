# --------------------------------------------------------------------------------------------------
# Utility applications cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> application: dbgsetup")

add_executable(tool_dbgsetup dbgsetup.c)
target_link_libraries(tool_dbgsetup PRIVATE lib_app_cli lib_json lib_log)
