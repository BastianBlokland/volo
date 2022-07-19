# --------------------------------------------------------------------------------------------------
# Application library cmake file.
# Provides entry point implementations for the various kinds of applications.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: app")

#
# Cli applications entry point.
#
add_library(lib_app_cli STATIC src/main_cli.c)
target_include_directories(lib_app_cli PUBLIC include)
target_link_libraries(lib_app_cli PUBLIC lib_cli)
target_link_libraries(lib_app_cli PUBLIC lib_core)
target_link_libraries(lib_app_cli PRIVATE lib_jobs)
target_link_libraries(lib_app_cli PRIVATE lib_log)

#
# Check (unit test library) applications entry point.
#
add_library(lib_app_check STATIC src/main_cli.c src/main_check.c)
target_include_directories(lib_app_check PUBLIC include)
target_link_libraries(lib_app_check PRIVATE lib_cli)
target_link_libraries(lib_app_check PRIVATE lib_log)
target_link_libraries(lib_app_check PRIVATE lib_jobs)
target_link_libraries(lib_app_check PUBLIC lib_check)
