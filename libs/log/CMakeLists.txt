# --------------------------------------------------------------------------------------------------
# Log library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "Configuring log library")

add_library(volo_log STATIC
  src/api.c)
target_include_directories(volo_log PUBLIC include)
target_link_libraries(volo_log PUBLIC volo_core)
target_link_libraries(volo_log PUBLIC volo_json)

add_executable(volo_log_test
  test/main.c
  test/test_api.c)
target_link_libraries(volo_log_test PRIVATE volo_check)
target_link_libraries(volo_log_test PRIVATE volo_cli)
target_link_libraries(volo_log_test PRIVATE volo_log)

configure_test(volo_log_test log)
