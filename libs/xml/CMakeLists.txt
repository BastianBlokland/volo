# --------------------------------------------------------------------------------------------------
# Xml library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(xml STATIC
  src/doc.c
  src/eq.c
  src/lex.c
  src/read.c
  src/write.c
  )
target_include_directories(xml PUBLIC include)
target_link_libraries(xml PUBLIC core)

add_executable(xml_test
  test/config.c
  test/test_doc.c
  test/test_eq.c
  test/test_read.c
  test/test_write.c
  )
target_link_libraries(xml_test PRIVATE app_check xml)
