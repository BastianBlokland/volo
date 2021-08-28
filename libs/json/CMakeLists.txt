# --------------------------------------------------------------------------------------------------
# Json library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "Configuring json library")

add_library(volo_json STATIC
  src/doc.c
  src/eq.c
  src/lex.c
  src/parse.c)
target_include_directories(volo_json PUBLIC include)
target_link_libraries(volo_json PUBLIC volo_core)

add_executable(volo_json_test
  test/main.c
  test/test_doc.c
  test/test_eq.c
  test/test_parse.c)
target_link_libraries(volo_json_test PRIVATE volo_check)
target_link_libraries(volo_json_test PRIVATE volo_cli)
target_link_libraries(volo_json_test PRIVATE volo_json)

configure_test(volo_json_test json)
