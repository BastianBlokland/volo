#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "geo_nav.h"

#include "intrinsic_internal.h"

typedef struct {
  u8 blockers;
} GeoNavCellData;

struct sGeoNavGrid {
  u32             cellCountAxis;
  u32             cellCountTotal;
  f32             cellDensity; // 1.0 / cellSize
  f32             cellSize;    // 1.0 / cellDensity
  f32             cellHeight;
  GeoVector       cellOffset;
  GeoNavCellData* cells;
  Allocator*      alloc;
};

static GeoNavCellData* nav_cell_data(GeoNavGrid* grid, const GeoNavCell cell) {
  return &grid->cells[cell.y * grid->cellCountAxis + cell.x];
}

static const GeoNavCellData* nav_cell_data_readonly(const GeoNavGrid* grid, const GeoNavCell cell) {
  return nav_cell_data((GeoNavGrid*)grid, cell);
}

static GeoVector nav_cell_pos(const GeoNavGrid* grid, const GeoNavCell cell) {
  return geo_vector(
      cell.x * grid->cellSize + grid->cellOffset.x,
      grid->cellOffset.y,
      cell.y * grid->cellSize + grid->cellOffset.z);
}

static GeoBox nav_cell_box(const GeoNavGrid* grid, const GeoNavCell cell) {
  const GeoVector center = nav_cell_pos(grid, cell);
  return geo_box_from_center(center, geo_vector(grid->cellSize, grid->cellHeight, grid->cellSize));
}

typedef enum {
  GeoNavMap_ClampedX = 1 << 0,
  GeoNavMap_ClampedY = 1 << 1,
} GeoNavMapFlags;

typedef struct {
  GeoNavCell     cell;
  GeoNavMapFlags flags;
} GeoNavMapResult;

static GeoNavMapResult nav_cell_map(const GeoNavGrid* grid, const GeoVector pos) {
  GeoNavMapResult result = {0};
  const f32       localX = intrinsic_round_f32((pos.x - grid->cellOffset.x) * grid->cellDensity);
  const f32       localY = intrinsic_round_f32((pos.z - grid->cellOffset.z) * grid->cellDensity);
  if (UNLIKELY(localX < 0)) {
    result.flags |= GeoNavMap_ClampedX;
  } else {
    result.cell.x = (u16)localX;
    if (UNLIKELY(result.cell.x >= grid->cellCountAxis)) {
      result.flags |= GeoNavMap_ClampedX;
      result.cell.x = grid->cellCountAxis - 1;
    }
  }
  if (UNLIKELY(localY < 0)) {
    result.flags |= GeoNavMap_ClampedY;
  } else {
    result.cell.y = (u16)localY;
    if (UNLIKELY(result.cell.y >= grid->cellCountAxis)) {
      result.flags |= GeoNavMap_ClampedY;
      result.cell.y = grid->cellCountAxis - 1;
    }
  }
  return result;
}

static GeoNavRegion nav_cell_map_box(const GeoNavGrid* grid, const GeoBox* box) {
  const GeoNavMapResult resMin = nav_cell_map(grid, box->min);
  GeoNavMapResult       resMax = nav_cell_map(grid, box->max);
  if (LIKELY((resMin.flags & resMax.flags & GeoNavMap_ClampedX) == 0)) {
    ++resMax.cell.x; // +1 because max is exclusive.
  }
  if (LIKELY((resMin.flags & resMax.flags & GeoNavMap_ClampedY) == 0)) {
    ++resMax.cell.y; // +1 because max is exclusive.
  }
  return (GeoNavRegion){.min = resMin.cell, .max = resMax.cell};
}

static void nav_cell_block(GeoNavGrid* grid, const GeoNavCell cell) {
  ++nav_cell_data(grid, cell)->blockers;
}

static void nav_clear_cells(GeoNavGrid* grid) {
  const Mem cellsMem = mem_create(grid->cells, grid->cellCountTotal * sizeof(GeoNavCellData));
  mem_set(cellsMem, 0);
}

GeoNavGrid* geo_nav_grid_create(
    Allocator* alloc, const GeoVector center, const f32 size, const f32 density, const f32 height) {
  diag_assert(geo_vector_mag_sqr(center) <= (1e4f * 1e4f));
  diag_assert(size > 1e-4f && size < 1e4f);
  diag_assert(density > 1e-4f && density < 1e4f);

  GeoNavGrid* grid           = alloc_alloc_t(alloc, GeoNavGrid);
  const u32   cellCountAxis  = (u32)math_round_f32(size * density);
  const u32   cellCountTotal = cellCountAxis * cellCountAxis;

  *grid = (GeoNavGrid){
      .cellCountAxis  = cellCountAxis,
      .cellCountTotal = cellCountTotal,
      .cellDensity    = density,
      .cellSize       = 1.0f / density,
      .cellHeight     = height,
      .cellOffset     = geo_vector(size * -0.5f - center.x, center.y, size * -0.5f - center.z),
      .cells          = alloc_array_t(alloc, GeoNavCellData, cellCountTotal),
      .alloc          = alloc,
  };

  nav_clear_cells(grid);
  return grid;
}

void geo_nav_grid_destroy(GeoNavGrid* grid) {
  alloc_free_array_t(grid->alloc, grid->cells, grid->cellCountTotal);
  alloc_free_t(grid->alloc, grid);
}

GeoNavRegion geo_nav_bounds(const GeoNavGrid* grid) {
  return (GeoNavRegion){.max = {.x = grid->cellCountAxis, .y = grid->cellCountAxis}};
}

GeoVector geo_nav_cell_size(const GeoNavGrid* grid) {
  return geo_vector(grid->cellSize, grid->cellHeight, grid->cellSize);
}

GeoVector geo_nav_position(const GeoNavGrid* grid, const GeoNavCell cell) {
  diag_assert(cell.x < grid->cellCountAxis);
  diag_assert(cell.y < grid->cellCountAxis);
  return nav_cell_pos(grid, cell);
}

GeoBox geo_nav_box(const GeoNavGrid* grid, const GeoNavCell cell) {
  diag_assert(cell.x < grid->cellCountAxis);
  diag_assert(cell.y < grid->cellCountAxis);
  return nav_cell_box(grid, cell);
}

bool geo_nav_blocked(const GeoNavGrid* grid, const GeoNavCell cell) {
  diag_assert(cell.x < grid->cellCountAxis);
  diag_assert(cell.y < grid->cellCountAxis);
  return nav_cell_data_readonly(grid, cell)->blockers > 0;
}

GeoNavCell geo_nav_at_position(const GeoNavGrid* grid, const GeoVector pos) {
  return nav_cell_map(grid, pos).cell;
}

void geo_nav_blocker_clear_all(GeoNavGrid* grid) { nav_clear_cells(grid); }

void geo_nav_blocker_add_box(GeoNavGrid* grid, const GeoBox* box) {
  if (box->max.y < 0 || box->min.y > grid->cellHeight) {
    return; // Outside of the y band of the grid.
  }
  const GeoNavRegion region = nav_cell_map_box(grid, box);
  for (u32 y = region.min.y; y != region.max.y; ++y) {
    for (u32 x = region.min.x; x != region.max.x; ++x) {
      const GeoNavCell cell = {.x = x, .y = y};
      nav_cell_block(grid, cell);
    }
  }
}

void geo_nav_blocker_add_box_rotated(GeoNavGrid* grid, const GeoBoxRotated* boxRotated) {
  const GeoBox bounds = geo_box_from_rotated(&boxRotated->box, boxRotated->rotation);

  const GeoNavRegion region = nav_cell_map_box(grid, &bounds);
  for (u32 y = region.min.y; y != region.max.y; ++y) {
    for (u32 x = region.min.x; x != region.max.x; ++x) {
      const GeoNavCell cell    = {.x = x, .y = y};
      const GeoBox     cellBox = nav_cell_box(grid, cell);
      if (geo_box_rotated_overlap_box(boxRotated, &cellBox)) {
        nav_cell_block(grid, cell);
      }
    }
  }
}
