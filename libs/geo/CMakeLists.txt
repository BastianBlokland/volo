# --------------------------------------------------------------------------------------------------
# Geometry library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: geo")

add_library(lib_geo STATIC
  src/box.c
  src/color.c
  src/line.c
  src/matrix.c
  src/plane.c
  src/quat.c
  src/query.c
  src/sphere.c
  src/vector.c
  )
target_include_directories(lib_geo PUBLIC include)
target_link_libraries(lib_geo PUBLIC lib_core)

add_executable(lib_geo_test
  test/main.c
  test/test_box.c
  test/test_line.c
  test/test_matrix.c
  test/test_plane.c
  test/test_quat.c
  test/test_sphere.c
  test/test_vector.c
  test/utils.c
  )
target_link_libraries(lib_geo_test PRIVATE lib_check)
target_link_libraries(lib_geo_test PRIVATE lib_geo)
target_link_libraries(lib_geo_test PRIVATE lib_jobs)
target_link_libraries(lib_geo_test PRIVATE lib_log)

configure_test(lib_geo_test SHORT_NAME geo)
configure_debuggable(lib_geo_test)
