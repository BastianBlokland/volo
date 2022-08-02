# --------------------------------------------------------------------------------------------------
# Geometry library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: geo")

add_library(lib_geo STATIC
  src/box_rotated.c
  src/box.c
  src/capsule.c
  src/color.c
  src/line.c
  src/matrix.c
  src/nav.c
  src/plane.c
  src/quat.c
  src/query.c
  src/ray.c
  src/sphere.c
  src/vector.c
  )
target_include_directories(lib_geo PUBLIC include)
target_link_libraries(lib_geo PUBLIC lib_core)

add_executable(test_lib_geo
  test/config.c
  test/test_box.c
  test/test_line.c
  test/test_matrix.c
  test/test_plane.c
  test/test_quat.c
  test/test_sphere.c
  test/test_vector.c
  test/utils.c
  )
target_link_libraries(test_lib_geo PRIVATE lib_app_check lib_geo)

configure_test(test_lib_geo SHORT_NAME geo)
configure_debuggable(test_lib_geo)
