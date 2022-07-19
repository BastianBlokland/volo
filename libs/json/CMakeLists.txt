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

add_executable(test_lib_json
  test/config.c
  test/test_doc.c
  test/test_eq.c
  test/test_read.c
  test/test_write.c
  )
target_link_libraries(test_lib_json PRIVATE lib_app_check lib_json)

configure_test(test_lib_json SHORT_NAME json)
configure_debuggable(test_lib_json)
