# --------------------------------------------------------------------------------------------------
# AI library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: ai")

add_library(lib_ai STATIC
  src/blackboard.c
  )
target_include_directories(lib_ai PUBLIC include)
target_link_libraries(lib_ai PUBLIC lib_core lib_geo)
target_link_libraries(lib_ai PRIVATE lib_data)

add_executable(test_lib_ai
  test/config.c
  test/test_blackboard.c
  )
target_link_libraries(test_lib_ai PRIVATE lib_app_check lib_ai)

configure_test(test_lib_ai SHORT_NAME ai)
configure_debuggable(test_lib_ai)
