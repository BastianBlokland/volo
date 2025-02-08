# --------------------------------------------------------------------------------------------------
# Xml library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: xml")

add_library(lib_xml STATIC
  src/doc.c
  src/eq.c
  src/lex.c
  src/read.c
  )
target_include_directories(lib_xml PUBLIC include)
target_link_libraries(lib_xml PUBLIC lib_core)

add_executable(test_lib_xml
  test/config.c
  test/test_doc.c
  test/test_eq.c
  test/test_read.c
  )
target_link_libraries(test_lib_xml PRIVATE lib_app_check lib_xml)

configure_test(test_lib_xml SHORT_NAME xml)
configure_debuggable(test_lib_xml)
