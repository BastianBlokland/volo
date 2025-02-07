# --------------------------------------------------------------------------------------------------
# Xml library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: xml")

add_library(lib_xml STATIC
  src/doc.c
  )
target_include_directories(lib_xml PUBLIC include)
target_link_libraries(lib_xml PUBLIC lib_core)
