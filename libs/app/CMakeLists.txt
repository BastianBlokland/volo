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
target_link_libraries(lib_app_cli PUBLIC lib_core lib_cli)
target_link_libraries(lib_app_cli PRIVATE lib_log lib_data)

#
# Check applications entry point.
#
add_library(lib_app_check STATIC src/app_cli.c src/app_check.c)
target_include_directories(lib_app_check PUBLIC include)
target_link_libraries(lib_app_check PUBLIC lib_core lib_check)
target_link_libraries(lib_app_check PRIVATE lib_cli lib_log lib_data lib_jobs lib_trace)

#
# Ecs applications entry point.
#
add_library(lib_app_ecs STATIC src/app_cli.c src/app_ecs.c)
target_include_directories(lib_app_ecs PUBLIC include)
target_link_libraries(lib_app_ecs PUBLIC lib_core lib_cli lib_ecs)
target_link_libraries(lib_app_ecs PRIVATE lib_log lib_data lib_jobs lib_trace)
