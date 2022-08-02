#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "geo_nav.h"

#include "intrinsic_internal.h"

typedef struct {
  u32 dummy;
} GeoNavCellData;

struct sGeoNavEnv {
  u32             cellCountAxis;
  u32             cellCountTotal;
  f32             cellDensity;
  f32             cellSize; // 1.0 / cellDensity
  GeoVector       cellOffset;
  GeoNavCellData* cells;
  Allocator*      alloc;
};

// static GeoNavCellData* nav_cell_data(GeoNavEnv* env, const GeoNavCell cell) {
//   return &env->cells[cell.y * env->cellCountAxis + cell.x];
// }

static GeoNavCell nav_cell_clamp(const GeoNavEnv* env, GeoNavCell cell) {
  if (cell.x >= env->cellCountAxis) {
    cell.x = env->cellCountAxis - 1;
  }
  if (cell.y >= env->cellCountAxis) {
    cell.y = env->cellCountAxis - 1;
  }
  return cell;
}

static GeoVector nav_cell_pos(const GeoNavEnv* env, const GeoNavCell cell) {
  return geo_vector(
      cell.x * env->cellSize + env->cellOffset.x,
      env->cellOffset.y,
      cell.y * env->cellSize + env->cellOffset.z);
}

static GeoNavCell nav_cell_from_pos(const GeoNavEnv* env, const GeoVector pos) {
  return (GeoNavCell){
      .x = (u16)intrinsic_round_f32((pos.x - env->cellOffset.x) * env->cellDensity),
      .y = (u16)intrinsic_round_f32((pos.z - env->cellOffset.z) * env->cellDensity),
  };
}

static GeoNavCell nav_cell_from_pos_clamped(const GeoNavEnv* env, const GeoVector pos) {
  return nav_cell_clamp(env, nav_cell_from_pos(env, pos));
}

static void nav_clear_cells(GeoNavEnv* env) {
  const Mem cellsMem = mem_create(env->cells, env->cellCountTotal * sizeof(GeoNavCellData));
  mem_set(cellsMem, 0);
}

GeoNavEnv*
geo_nav_env_create(Allocator* alloc, const GeoVector center, const f32 size, const f32 density) {
  diag_assert(geo_vector_mag_sqr(center) <= (1e4f * 1e4f));
  diag_assert(size > 1e-4f && size < 1e4f);
  diag_assert(density > 1e-4f && density < 1e4f);

  GeoNavEnv*  env            = alloc_alloc_t(alloc, GeoNavEnv);
  const u32   cellCountAxis  = (u32)math_round_f32(size * density);
  const u32   cellCountTotal = cellCountAxis * cellCountAxis;
  const usize cellMemSize    = cellCountTotal * sizeof(GeoNavCellData);

  *env = (GeoNavEnv){
      .cellCountAxis  = cellCountAxis,
      .cellCountTotal = cellCountTotal,
      .cellDensity    = density,
      .cellSize       = 1.0f / density,
      .cellOffset     = geo_vector(size * -0.5f - center.x, center.y, size * -0.5f - center.z),
      .cells          = alloc_array_t(alloc, GeoNavCellData, cellMemSize),
      .alloc          = alloc,
  };

  nav_clear_cells(env);
  return env;
}

void geo_nav_env_destroy(GeoNavEnv* env) {
  alloc_free_array_t(env->alloc, env->cells, env->cellCountTotal);
  alloc_free_t(env->alloc, env);
}

GeoVector geo_nav_position(const GeoNavEnv* env, const GeoNavCell cell) {
  return nav_cell_pos(env, cell);
}

GeoNavCell geo_nav_cell_from_position(const GeoNavEnv* env, const GeoVector pos) {
  return nav_cell_from_pos_clamped(env, pos);
}
