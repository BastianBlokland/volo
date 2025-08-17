# --------------------------------------------------------------------------------------------------
# Geometry library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(geo STATIC
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
target_include_directories(geo PUBLIC include)
target_link_libraries(geo PUBLIC core)
target_link_libraries(geo PRIVATE jobs log)

add_executable(test_geo
  test/config.c
  test/test_box_rotated.c
  test/test_box.c
  test/test_capsule.c
  test/test_color.c
  test/test_line.c
  test/test_matrix.c
  test/test_nav.c
  test/test_plane.c
  test/test_quat.c
  test/test_sphere.c
  test/test_vector.c
  test/utils.c
  )
target_link_libraries(test_geo PRIVATE app_check geo)

configure_debuggable(test_geo)
