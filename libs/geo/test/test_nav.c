#include "check_spec.h"
#include "core_alloc.h"
#include "geo_nav.h"

#include "utils_internal.h"

spec(nav) {

  const GeoVector center   = {10, 42, -10};
  const f32       size     = 10;
  const f32       density  = 0.5f;
  const f32       cellSize = 1.0f / density;
  const f32       height   = 0.5f;
  GeoNavGrid*     grid     = null;

  setup() { grid = geo_nav_grid_create(g_alloc_heap, center, size, density, height); }

  it("can retrieve the bounding region") {
    const GeoNavRegion region = geo_nav_bounds(grid);
    check_eq_int(region.min.x, 0);
    check_eq_int(region.min.y, 0);
    check_eq_int(region.max.x, (u16)math_round_f32(size * density));
    check_eq_int(region.max.y, (u16)math_round_f32(size * density));
  }

  it("can retrieve the cell size") {
    check_eq_vector(geo_nav_cell_size(grid), geo_vector(cellSize, height, cellSize));
  }

  it("can convert between coordinates and cells") {
    const GeoVector posA = geo_nav_position(grid, (GeoNavCell){.x = 0, .y = 0});
    check_eq_vector(posA, geo_vector(center.x + size * -0.5f, center.y, center.z + size * -0.5f));
    check_eq_int(geo_nav_at_position(grid, posA).x, 0);
    check_eq_int(geo_nav_at_position(grid, posA).y, 0);

    const GeoVector posB = geo_nav_position(grid, (GeoNavCell){.x = 1, .y = 0});
    check_eq_vector(posB, geo_vector(posA.x + cellSize, center.y, posA.z));
    check_eq_int(geo_nav_at_position(grid, posB).x, 1);
    check_eq_int(geo_nav_at_position(grid, posB).y, 0);

    const GeoVector posC = geo_nav_position(grid, (GeoNavCell){.x = 4, .y = 0});
    check_eq_vector(posC, geo_vector(posA.x + cellSize * 4, center.y, posA.z));
    check_eq_int(geo_nav_at_position(grid, posC).x, 4);
    check_eq_int(geo_nav_at_position(grid, posC).y, 0);

    const GeoVector posD = geo_nav_position(grid, (GeoNavCell){.x = 0, .y = 3});
    check_eq_vector(posD, geo_vector(posA.x, center.y, posA.z + cellSize * 3));
    check_eq_int(geo_nav_at_position(grid, posD).x, 0);
    check_eq_int(geo_nav_at_position(grid, posD).y, 3);
  }

  it("clamps coordinates to the grid edges") {
    check_eq_int(geo_nav_at_position(grid, geo_vector(5, 0, 0)).x, 0);
    check_eq_int(geo_nav_at_position(grid, geo_vector(4, 0, 0)).x, 0);

    check_eq_int(geo_nav_at_position(grid, geo_vector(0, 0, -15)).y, 0);
    check_eq_int(geo_nav_at_position(grid, geo_vector(0, 0, -16)).y, 0);

    check_eq_int(geo_nav_at_position(grid, geo_vector(15, 0, 0)).x, 4);
    check_eq_int(geo_nav_at_position(grid, geo_vector(16, 0, 0)).x, 4);

    check_eq_int(geo_nav_at_position(grid, geo_vector(0, 0, 15)).y, 4);
    check_eq_int(geo_nav_at_position(grid, geo_vector(0, 0, 16)).y, 4);
  }

  teardown() { geo_nav_grid_destroy(grid); }
}
