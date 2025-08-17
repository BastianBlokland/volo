# --------------------------------------------------------------------------------------------------
# Cli library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(cli STATIC
  src/app.c
  src/failure.c
  src/help.c
  src/parse.c
  src/read.c
  src/validate.c
  )
target_include_directories(cli PUBLIC include)
target_link_libraries(cli PUBLIC core)

add_executable(cli_test
  test/config.c
  test/test_app.c
  test/test_failure.c
  test/test_help.c
  test/test_parse.c
  test/test_read.c
  test/test_validate.c
  )
target_link_libraries(cli_test PRIVATE app_check cli)
