# --------------------------------------------------------------------------------------------------
# Geometry library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: geo")

add_library(volo_geo STATIC
  src/matrix.c
  src/quat.c
  src/vector.c)
target_include_directories(volo_geo PUBLIC include)
target_link_libraries(volo_geo PUBLIC volo_core)

add_executable(volo_geo_test
  test/main.c
  test/test_matrix.c
  test/test_quat.c
  test/test_vector.c
  test/utils.c)
target_link_libraries(volo_geo_test PRIVATE volo_check)
target_link_libraries(volo_geo_test PRIVATE volo_geo)
target_link_libraries(volo_geo_test PRIVATE volo_jobs)
target_link_libraries(volo_geo_test PRIVATE volo_log)

configure_test(volo_geo_test geo)
configure_debuggable(volo_geo_test)
