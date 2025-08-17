# --------------------------------------------------------------------------------------------------
# Json library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(json STATIC
  src/doc.c
  src/eq.c
  src/lex.c
  src/read.c
  src/write.c
  )
target_include_directories(json PUBLIC include)
target_link_libraries(json PUBLIC core)

add_executable(test_json
  test/config.c
  test/test_doc.c
  test/test_eq.c
  test/test_read.c
  test/test_write.c
  )
target_link_libraries(test_json PRIVATE app_check json)

configure_test(test_json SHORT_NAME json)
configure_debuggable(test_json)
