#include "check_spec.h"
#include "core_alloc.h"
#include "core_math.h"
#include "geo_box.h"
#include "geo_nav.h"
#include "geo_vector.h"

#include "utils_internal.h"

spec(nav) {

  const f32   size        = 10;
  const f32   cellSize    = 2.0f;
  const f32   density     = 1.0f / cellSize;
  const f32   height      = 0.5f;
  const f32   blockHeight = 0.5f;
  GeoNavGrid* grid        = null;

  setup() { grid = geo_nav_grid_create(g_allocHeap, size, cellSize, height, blockHeight); }

  it("can retrieve the bounding region") {
    const GeoNavRegion region = geo_nav_bounds(grid);
    check_eq_int(region.min.x, 0);
    check_eq_int(region.min.y, 0);
    check_eq_int(region.max.x, (u16)math_round_nearest_f32(size * density));
    check_eq_int(region.max.y, (u16)math_round_nearest_f32(size * density));
  }

  it("can convert between coordinates and cells") {
    const GeoVector posA = geo_nav_position(grid, (GeoNavCell){.x = 0, .y = 0});
    check_eq_vector(posA, geo_vector(+size * -0.5f, 0, size * -0.5f));
    check_eq_int(geo_nav_at_position(grid, posA).x, 0);
    check_eq_int(geo_nav_at_position(grid, posA).y, 0);

    const GeoVector posB = geo_nav_position(grid, (GeoNavCell){.x = 1, .y = 0});
    check_eq_vector(posB, geo_vector(posA.x + cellSize, 0, posA.z));
    check_eq_int(geo_nav_at_position(grid, posB).x, 1);
    check_eq_int(geo_nav_at_position(grid, posB).y, 0);

    const GeoVector posC = geo_nav_position(grid, (GeoNavCell){.x = 4, .y = 0});
    check_eq_vector(posC, geo_vector(posA.x + cellSize * 4, 0, posA.z));
    check_eq_int(geo_nav_at_position(grid, posC).x, 4);
    check_eq_int(geo_nav_at_position(grid, posC).y, 0);

    const GeoVector posD = geo_nav_position(grid, (GeoNavCell){.x = 0, .y = 3});
    check_eq_vector(posD, geo_vector(posA.x, 0, posA.z + cellSize * 3));
    check_eq_int(geo_nav_at_position(grid, posD).x, 0);
    check_eq_int(geo_nav_at_position(grid, posD).y, 3);
  }

  it("clamps coordinates to the grid edges") {
    check_eq_int(geo_nav_at_position(grid, geo_vector(5, 0, 0)).x, 4);
    check_eq_int(geo_nav_at_position(grid, geo_vector(4, 0, 0)).x, 4);

    check_eq_int(geo_nav_at_position(grid, geo_vector(0, 0, -15)).y, 0);
    check_eq_int(geo_nav_at_position(grid, geo_vector(0, 0, -16)).y, 0);

    check_eq_int(geo_nav_at_position(grid, geo_vector(15, 0, 0)).x, 4);
    check_eq_int(geo_nav_at_position(grid, geo_vector(16, 0, 0)).x, 4);

    check_eq_int(geo_nav_at_position(grid, geo_vector(0, 0, 15)).y, 4);
    check_eq_int(geo_nav_at_position(grid, geo_vector(0, 0, 16)).y, 4);
  }

  it("can block a single cell") {
    const GeoNavCell cell = {.x = 2, .y = 2};
    check(!geo_nav_check(grid, cell, GeoNavCond_Blocked));

    const GeoBlockerShape shape = {
        .type = GeoBlockerType_Box,
        .box  = geo_box_from_sphere(geo_nav_position(grid, cell), 0.25f),
    };
    geo_nav_blocker_add(grid, 42, &shape, 1);

    check(geo_nav_check(grid, cell, GeoNavCond_Blocked));
    check(!geo_nav_check(grid, (GeoNavCell){.x = 3, .y = 2}, GeoNavCond_Blocked));
    check(!geo_nav_check(grid, (GeoNavCell){.x = 1, .y = 2}, GeoNavCond_Blocked));
    check(!geo_nav_check(grid, (GeoNavCell){.x = 2, .y = 3}, GeoNavCond_Blocked));
    check(!geo_nav_check(grid, (GeoNavCell){.x = 2, .y = 1}, GeoNavCond_Blocked));
  }

  it("ignores blockers below the grid") {
    const GeoNavCell cell = {.x = 2, .y = 2};
    check(!geo_nav_check(grid, cell, GeoNavCond_Blocked));

    const GeoVector       pos = geo_vector_sub(geo_nav_position(grid, cell), geo_vector(0, -1, 0));
    const GeoBlockerShape shape = {
        .type = GeoBlockerType_Box,
        .box  = geo_box_from_sphere(pos, 0.25f),
    };
    geo_nav_blocker_add(grid, 42, &shape, 1);

    check(!geo_nav_check(grid, cell, GeoNavCond_Blocked));
  }

  it("ignores blockers above the cell height") {
    const GeoNavCell cell = {.x = 2, .y = 2};
    check(!geo_nav_check(grid, cell, GeoNavCond_Blocked));

    const GeoVector       pos   = geo_vector_add(geo_nav_position(grid, cell), geo_vector(0, 1, 0));
    const GeoBlockerShape shape = {
        .type = GeoBlockerType_Box,
        .box  = geo_box_from_sphere(pos, 0.25f),
    };
    geo_nav_blocker_add(grid, 42, &shape, 1);

    check(!geo_nav_check(grid, cell, GeoNavCond_Blocked));
  }

  it("blocks cells if the y position is too hight") {
    const GeoNavCell cell = {.x = 2, .y = 2};
    check(!geo_nav_check(grid, cell, GeoNavCond_Blocked));

    geo_nav_y_update(grid, cell, 1.0f);

    check(geo_nav_check(grid, cell, GeoNavCond_Blocked));
  }

  it("unblocks cells if the y position is lowered again") {
    const GeoNavCell cell = {.x = 2, .y = 2};
    check(!geo_nav_check(grid, cell, GeoNavCond_Blocked));

    geo_nav_y_update(grid, cell, 1.0f);
    check(geo_nav_check(grid, cell, GeoNavCond_Blocked));

    geo_nav_y_update(grid, cell, 0.4f);
    check(!geo_nav_check(grid, cell, GeoNavCond_Blocked));
  }

  it("can find the closest unblocked cell") {
    const GeoNavCell cell = {.x = 2, .y = 2};

    const GeoBlockerShape shape = {
        .type = GeoBlockerType_Box,
        .box  = geo_box_from_sphere(geo_nav_position(grid, cell), 2.0f),
    };
    geo_nav_blocker_add(grid, 42, &shape, 1);

    check(geo_nav_check(grid, cell, GeoNavCond_Blocked));
    check(geo_nav_check(grid, (GeoNavCell){.x = 3, .y = 2}, GeoNavCond_Blocked));
    check(geo_nav_check(grid, (GeoNavCell){.x = 1, .y = 2}, GeoNavCond_Blocked));
    check(geo_nav_check(grid, (GeoNavCell){.x = 2, .y = 3}, GeoNavCond_Blocked));
    check(geo_nav_check(grid, (GeoNavCell){.x = 2, .y = 1}, GeoNavCond_Blocked));

    const GeoNavCell closestUnblocked = geo_nav_closest(grid, cell, GeoNavCond_Unblocked);

    check_eq_int(closestUnblocked.x, 4);
    check_eq_int(closestUnblocked.y, 2);
    check(!geo_nav_check(grid, closestUnblocked, GeoNavCond_Blocked));
  }

  teardown() { geo_nav_grid_destroy(grid); }
}
