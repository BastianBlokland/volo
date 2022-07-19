# --------------------------------------------------------------------------------------------------
# Application library cmake file.
# Provides entry point implementations for the various kinds of applications.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: app")

#
# Cli applications entry point.
#
add_library(lib_app_cli STATIC src/app_cli.c)
target_include_directories(lib_app_cli PUBLIC include)
target_link_libraries(lib_app_cli PUBLIC lib_cli lib_core)
target_link_libraries(lib_app_cli PRIVATE lib_jobs lib_log)

#
# Check (unit test library) applications entry point.
#
add_library(lib_app_check STATIC src/app_cli.c src/app_check.c)
target_include_directories(lib_app_check PUBLIC include)
target_link_libraries(lib_app_check PUBLIC lib_check)
target_link_libraries(lib_app_check PRIVATE lib_cli lib_log lib_jobs)
