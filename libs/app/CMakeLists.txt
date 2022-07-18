# --------------------------------------------------------------------------------------------------
# Application library cmake file.
# Provides entry point implementations for the various kinds of applications.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: app")

add_library(lib_app_cli STATIC src/main_cli.c)
target_include_directories(lib_app_cli PUBLIC include)
target_link_libraries(lib_app_cli PUBLIC lib_cli)
target_link_libraries(lib_app_cli PUBLIC lib_core)
target_link_libraries(lib_app_cli PRIVATE lib_jobs)
target_link_libraries(lib_app_cli PRIVATE lib_log)
