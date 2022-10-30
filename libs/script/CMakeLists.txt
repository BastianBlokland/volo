# --------------------------------------------------------------------------------------------------
# Script library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: script")

add_library(lib_script STATIC
  src/doc.c
  src/error.c
  src/lex.c
  src/val.c
  )
target_include_directories(lib_script PUBLIC include)
target_link_libraries(lib_script PUBLIC lib_core lib_geo lib_ecs)

add_executable(test_lib_script
  test/config.c
  test/test_lex.c
  test/test_val.c
  test/utils.c
  )
target_link_libraries(test_lib_script PRIVATE lib_app_check lib_script)

configure_test(test_lib_script SHORT_NAME script)
configure_debuggable(test_lib_script)
