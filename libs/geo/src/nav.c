#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "geo_nav.h"

#include "intrinsic_internal.h"

typedef struct {
  u32 dummy;
} GeoNavCellData;

struct sGeoNavGrid {
  u32             cellCountAxis;
  u32             cellCountTotal;
  f32             cellDensity;
  f32             cellSize; // 1.0 / cellDensity
  GeoVector       cellOffset;
  GeoNavCellData* cells;
  Allocator*      alloc;
};

// static GeoNavCellData* nav_cell_data(GeoNavGrid* grid, const GeoNavCell cell) {
//   return &grid->cells[cell.y * grid->cellCountAxis + cell.x];
// }

static GeoNavCell nav_cell_clamp(const GeoNavGrid* grid, GeoNavCell cell) {
  if (cell.x >= grid->cellCountAxis) {
    cell.x = grid->cellCountAxis - 1;
  }
  if (cell.y >= grid->cellCountAxis) {
    cell.y = grid->cellCountAxis - 1;
  }
  return cell;
}

static GeoVector nav_cell_pos(const GeoNavGrid* grid, const GeoNavCell cell) {
  return geo_vector(
      cell.x * grid->cellSize + grid->cellOffset.x,
      grid->cellOffset.y,
      cell.y * grid->cellSize + grid->cellOffset.z);
}

static GeoNavCell nav_cell_from_pos(const GeoNavGrid* grid, const GeoVector pos) {
  return (GeoNavCell){
      .x = (u16)intrinsic_round_f32((pos.x - grid->cellOffset.x) * grid->cellDensity),
      .y = (u16)intrinsic_round_f32((pos.z - grid->cellOffset.z) * grid->cellDensity),
  };
}

static GeoNavCell nav_cell_from_pos_clamped(const GeoNavGrid* grid, const GeoVector pos) {
  return nav_cell_clamp(grid, nav_cell_from_pos(grid, pos));
}

static void nav_clear_cells(GeoNavGrid* grid) {
  const Mem cellsMem = mem_create(grid->cells, grid->cellCountTotal * sizeof(GeoNavCellData));
  mem_set(cellsMem, 0);
}

GeoNavGrid*
geo_nav_grid_create(Allocator* alloc, const GeoVector center, const f32 size, const f32 density) {
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

GeoVector geo_nav_position(const GeoNavGrid* grid, const GeoNavCell cell) {
  return nav_cell_pos(grid, cell);
}

GeoNavCell geo_nav_cell_from_position(const GeoNavGrid* grid, const GeoVector pos) {
  return nav_cell_from_pos_clamped(grid, pos);
}
