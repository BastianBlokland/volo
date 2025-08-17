# --------------------------------------------------------------------------------------------------
# Application library cmake file.
# Provides entry point implementations for the various kinds of applications.
# --------------------------------------------------------------------------------------------------

#
# Cli applications entry point.
#
add_library(app_cli STATIC src/app_cli.c)
target_include_directories(app_cli PUBLIC include)
target_link_libraries(app_cli PUBLIC core cli)
target_link_libraries(app_cli PRIVATE log data)

#
# Check applications entry point.
#
add_library(app_check STATIC src/app_cli.c src/app_check.c)
target_include_directories(app_check PUBLIC include)
target_link_libraries(app_check PUBLIC core check)
target_link_libraries(app_check PRIVATE cli log data jobs trace)

#
# Ecs applications entry point.
#
add_library(app_ecs STATIC src/app_cli.c src/app_ecs.c)
target_include_directories(app_ecs PUBLIC include)
target_link_libraries(app_ecs PUBLIC core cli ecs)
target_link_libraries(app_ecs PRIVATE log data trace)

if(${VOLO_PLATFORM} STREQUAL "linux")
  # For compat with older GLIBC versions, see: 'src/glibc_compat.c'.
  target_sources(app_cli PRIVATE "src/glibc_compat.c")
  target_link_options(app_cli PUBLIC -Wl,--wrap=__libc_start_main)

  # For compat with older GLIBC versions, see: 'src/glibc_compat.c'.
  target_sources(app_check PRIVATE "src/glibc_compat.c")
  target_link_options(app_check PUBLIC -Wl,--wrap=__libc_start_main)

  # For compat with older GLIBC versions, see: 'src/glibc_compat.c'.
  target_sources(app_ecs PRIVATE "src/glibc_compat.c")
  target_link_options(app_ecs PUBLIC -Wl,--wrap=__libc_start_main)
endif()
