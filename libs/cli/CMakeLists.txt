# --------------------------------------------------------------------------------------------------
# Cli library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: cli")

add_library(lib_cli STATIC
  src/app.c
  src/failure.c
  src/help.c
  src/parse.c
  src/read.c
  src/validate.c
  )
target_include_directories(lib_cli PUBLIC include)
target_link_libraries(lib_cli PUBLIC lib_core)

add_executable(test_lib_cli
  test/config.c
  test/test_app.c
  test/test_failure.c
  test/test_help.c
  test/test_parse.c
  test/test_read.c
  test/test_validate.c
  )
target_link_libraries(test_lib_cli PRIVATE lib_app_check lib_cli)

configure_test(test_lib_cli SHORT_NAME cli)
configure_debuggable(test_lib_cli)
