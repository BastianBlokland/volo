# --------------------------------------------------------------------------------------------------
# Json library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: json")

add_library(lib_json STATIC
  src/doc.c
  src/eq.c
  src/lex.c
  src/read.c
  src/write.c
  )
target_include_directories(lib_json PUBLIC include)
target_link_libraries(lib_json PUBLIC lib_core)

add_executable(lib_json_test
  test/main.c
  test/test_doc.c
  test/test_eq.c
  test/test_read.c
  test/test_write.c
  )
target_link_libraries(lib_json_test PRIVATE lib_check)
target_link_libraries(lib_json_test PRIVATE lib_jobs)
target_link_libraries(lib_json_test PRIVATE lib_json)
target_link_libraries(lib_json_test PRIVATE lib_log)

configure_test(lib_json_test SHORT_NAME json)
configure_debuggable(lib_json_test)
