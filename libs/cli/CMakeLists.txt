# --------------------------------------------------------------------------------------------------
# Cli library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "Configuring cli library")

add_library(volo_cli STATIC
  src/app.c)
target_include_directories(volo_cli PUBLIC include)
target_link_libraries(volo_cli PUBLIC volo_core)

add_executable(volo_cli_test
  test/main.c
  test/test_app.c)
target_link_libraries(volo_cli_test PRIVATE volo_jobs)
target_link_libraries(volo_cli_test PRIVATE volo_check)
