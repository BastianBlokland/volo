# --------------------------------------------------------------------------------------------------
# Json library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: json")

add_library(volo_json STATIC
  src/doc.c
  src/eq.c
  src/lex.c
  src/parse.c
  src/write.c)
target_include_directories(volo_json PUBLIC include)
target_link_libraries(volo_json PUBLIC volo_core)

add_executable(volo_json_test
  test/main.c
  test/test_doc.c
  test/test_eq.c
  test/test_parse.c
  test/test_write.c)
target_link_libraries(volo_json_test PRIVATE volo_check)
target_link_libraries(volo_json_test PRIVATE volo_json)
target_link_libraries(volo_json_test PRIVATE volo_jobs)
target_link_libraries(volo_json_test PRIVATE volo_log)

configure_test(volo_json_test json)
configure_debuggable(volo_json_test)
