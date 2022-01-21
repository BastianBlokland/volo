# --------------------------------------------------------------------------------------------------
# Cli library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: cli")

add_library(lib_cli STATIC
  src/app.c
  src/help.c
  src/failure.c
  src/parse.c
  src/read.c
  src/validate.c
  )
target_include_directories(lib_cli PUBLIC include)
target_link_libraries(lib_cli PUBLIC lib_core)

add_executable(lib_cli_test
  test/main.c
  test/test_app.c
  test/test_failure.c
  test/test_help.c
  test/test_parse.c
  test/test_read.c
  test/test_validate.c
  )
target_link_libraries(lib_cli_test PRIVATE lib_check)
target_link_libraries(lib_cli_test PRIVATE lib_cli)
target_link_libraries(lib_cli_test PRIVATE lib_jobs)
target_link_libraries(lib_cli_test PRIVATE lib_log)

configure_test(lib_cli_test cli)
configure_debuggable(lib_cli_test)
