# --------------------------------------------------------------------------------------------------
# Cli library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "Configuring cli library")

add_library(volo_cli STATIC
  src/app.c
  src/help.c
  src/failure.c
  src/parse.c
  src/read.c
  src/validate.c)
target_include_directories(volo_cli PUBLIC include)
target_link_libraries(volo_cli PUBLIC volo_core)

add_executable(volo_cli_test
  test/main.c
  test/test_app.c
  test/test_failure.c
  test/test_help.c
  test/test_parse.c
  test/test_read.c
  test/test_validate.c)
target_link_libraries(volo_cli_test PRIVATE volo_cli)
target_link_libraries(volo_cli_test PRIVATE volo_check)
target_link_libraries(volo_cli_test PRIVATE volo_log)

configure_test(volo_cli_test cli)
configure_debuggable(volo_cli_test)
