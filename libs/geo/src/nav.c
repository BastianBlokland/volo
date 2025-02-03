#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_intrinsic.h"
#include "core_math.h"
#include "core_rng.h"
#include "geo_box_rotated.h"
#include "geo_nav.h"
#include "geo_sphere.h"
#include "jobs.h"
#include "log_logger.h"

#define geo_nav_workers_max 8
#define geo_nav_occupants_max 4096
#define geo_nav_occupants_per_cell 3
#define geo_nav_blockers_max 2048
#define geo_nav_blocker_max_cells 512
#define geo_nav_island_max (u8_max - 1)
#define geo_nav_island_blocked u8_max
#define geo_nav_island_itr_per_tick 10000
#define geo_nav_path_queue_size 1024
#define geo_nav_path_iterations_max 10000
#define geo_nav_path_chebyshev_heuristic true
#define geo_nav_channel_radius_frac 0.4f

ASSERT(geo_nav_occupants_max < u16_max, "Nav occupant has to be indexable by a u16");
ASSERT(geo_nav_blockers_max < u16_max, "Nav blocker has to be indexable by a u16");
ASSERT((geo_nav_blockers_max & (geo_nav_blockers_max - 1u)) == 0, "Has to be a pow2");
ASSERT((geo_nav_blocker_max_cells & (geo_nav_blocker_max_cells - 1u)) == 0, "Has to be a pow2");

typedef bool (*NavCellPredicate)(const GeoNavGrid*, const void* ctx, u32 cellIndex);

typedef struct {
  u64                 userId;
  GeoNavOccupantFlags flags;
  f32                 radius, weight;
  f32                 pos[2]; // XZ position.
} GeoNavOccupant;

typedef struct {
  u64          userId;
  GeoNavRegion region;
  u8           blockedInRegion[bits_to_bytes(geo_nav_blocker_max_cells)];
} GeoNavBlocker;

typedef struct {
  BitSet      markedCells; // bit[cellCountTotal]
  GeoNavCell* cameFrom;    // GeoNavCell[cellCountTotal]
  u16*        costs;       // u16[cellCountTotal]
  u32         stats[GeoNavStat_Count];
} GeoNavWorkerState;

typedef enum {
  GeoNavIslandUpdater_Dirty  = 1 << 0,
  GeoNavIslandUpdater_Active = 1 << 1,

  GeoNavIslandUpdater_Busy = GeoNavIslandUpdater_Dirty | GeoNavIslandUpdater_Active,
} GeoNavIslandUpdaterFlags;

typedef struct {
  BitSet                   markedCells; // Marked cells already have their island updated.
  GeoNavCell               queue[1024];
  u32                      queueStart;
  u32                      queueEnd;
  GeoNavIslandUpdaterFlags flags : 8;
  GeoNavIsland             currentIsland;
  u16                      currentRegionY;
  u32                      currentItr;
} GeoNavIslandUpdater;

struct sGeoNavGrid {
  f32       size;
  u32       cellCountAxis, cellCountTotal;
  f32       cellSize, cellDensity;
  f32       cellHeight;
  f32       cellBlockHeight;
  GeoVector cellOffset;
  f32*      cellY;            // f32[cellCountTotal]
  u8*       cellBlockerCount; // u8[cellCountTotal]

  u16*   cellOccupancy;             // u16[cellCountTotal][geo_nav_occupants_per_cell]
  BitSet cellOccupiedStationarySet; // bit[cellCountTotal], cell has a non-moving occupant.

  GeoNavIsland* cellIslands; // GeoNavIsland[cellCountTotal]
  u32           islandCount;

  GeoNavBlocker* blockers;       // GeoNavBlocker[geo_nav_blockers_max]
  BitSet         blockerFreeSet; // bit[geo_nav_blockers_max]

  GeoNavOccupant* occupants; // GeoNavOccupant[geo_nav_occupants_max]
  u32             occupantCount;

  GeoNavIslandUpdater islandUpdater;

  GeoNavWorkerState* workerStates[geo_nav_workers_max];
  Allocator*         alloc;

  u32 stats[GeoNavStat_Count];
};

static GeoNavWorkerState* nav_worker_state_create(const GeoNavGrid* grid) {
  GeoNavWorkerState* state = alloc_alloc_t(grid->alloc, GeoNavWorkerState);

  *state = (GeoNavWorkerState){
      .markedCells = alloc_alloc(grid->alloc, bits_to_bytes(grid->cellCountTotal) + 1, 1),
      .cameFrom    = alloc_array_t(grid->alloc, GeoNavCell, grid->cellCountTotal),
      .costs       = alloc_array_t(grid->alloc, u16, grid->cellCountTotal),
  };
  return state;
}

INLINE_HINT static GeoNavWorkerState* nav_worker_state(const GeoNavGrid* grid) {
  diag_assert(g_jobsWorkerId < geo_nav_workers_max);
  return grid->workerStates[g_jobsWorkerId];
}

INLINE_HINT static u16 nav_abs_i16(const i16 v) {
  const i16 mask = v >> 15;
  return (v + mask) ^ mask;
}

INLINE_HINT static u16 nav_min_u16(const u16 a, const u16 b) { return a < b ? a : b; }

INLINE_HINT static void nav_bit_set(const BitSet bits, const u32 idx) {
  *mem_at_u8(bits, bits_to_bytes(idx)) |= 1u << bit_in_byte(idx);
}

INLINE_HINT static void nav_bit_clear(const BitSet bits, const u32 idx) {
  *mem_at_u8(bits, bits_to_bytes(idx)) &= ~(1u << bit_in_byte(idx));
}

INLINE_HINT static bool nav_bit_test(const BitSet bits, const u32 idx) {
  return (*mem_at_u8(bits, bits_to_bytes(idx)) & (1u << bit_in_byte(idx))) != 0;
}

typedef struct {
  f32 x, y;
} NavVec2D;

typedef struct {
  NavVec2D pos;    // XZ position.
  NavVec2D dirInv; // 1.0 / directionX, 1.0 / directionZ.
  f32      dist;
} NavLine2D;

INLINE_HINT static NavLine2D nav_line_create(const GeoVector a, const GeoVector b) {
  const NavVec2D delta   = {b.x - a.x, b.z - a.z};
  const f32      distSqr = delta.x * delta.x + delta.y * delta.y;
  const f32      dist    = intrinsic_sqrt_f32(distSqr);

  const NavVec2D dir = {
      math_abs(delta.x) > f32_epsilon ? delta.x / dist : f32_epsilon,
      math_abs(delta.y) > f32_epsilon ? delta.y / dist : f32_epsilon,
  };

  return (NavLine2D){
      .pos    = {a.x, a.z},
      .dirInv = {1.0f / dir.x, 1.0f / dir.y},
      .dist   = dist,
  };
}

typedef struct {
  NavVec2D pos;    // XZ position.
  f32      extent; // XZ extent.
} NavRect2D;

INLINE_HINT static bool nav_line_intersect_rect(const NavLine2D* l, const NavRect2D* r) {
  const NavVec2D min = {r->pos.x - r->extent, r->pos.y - r->extent};
  const NavVec2D max = {r->pos.x + r->extent, r->pos.y + r->extent};

  const f32 t1 = (min.x - l->pos.x) * l->dirInv.x;
  const f32 t2 = (max.x - l->pos.x) * l->dirInv.x;
  const f32 t3 = (min.y - l->pos.y) * l->dirInv.y;
  const f32 t4 = (max.y - l->pos.y) * l->dirInv.y;

  const f32 tMin = math_max(math_min(t1, t2), math_min(t3, t4));
  const f32 tMax = math_min(math_max(t1, t2), math_max(t3, t4));

  return tMax >= 0.0f && tMin <= tMax && tMin <= l->dist;
}

/**
 * Compute the total amount of cells in the region.
 */
INLINE_HINT static u32 nav_region_size(const GeoNavRegion region) {
  return (region.max.y - region.min.y) * (region.max.x - region.min.x);
}

INLINE_HINT static u32 nav_cell_index(const GeoNavGrid* grid, const GeoNavCell cell) {
  return (u32)cell.y * grid->cellCountAxis + cell.x;
}

INLINE_HINT static bool nav_cell_clamp_axis(const GeoNavGrid* grid, f32* value) {
  if (UNLIKELY(*value < 0)) {
    *value = 0;
    return true;
  }
  if (UNLIKELY(*value >= grid->cellCountAxis)) {
    *value = grid->cellCountAxis - 1;
    return true;
  }
  return false;
}

INLINE_HINT static GeoNavIsland nav_island(const GeoNavGrid* grid, const u32 cellIndex) {
  return grid->cellIslands[cellIndex];
}

static Mem nav_occupancy_mem(GeoNavGrid* grid) {
  const usize size = sizeof(u16) * grid->cellCountTotal * geo_nav_occupants_per_cell;
  return mem_create(grid->cellOccupancy, size);
}

static u32 nav_cell_neighbors(
    const GeoNavGrid* grid, const GeoNavCell cell, GeoNavCell out[PARAM_ARRAY_SIZE(4)]) {
  u32 count = 0;
  if (LIKELY((u16)(cell.x + 1) < grid->cellCountAxis)) {
    out[count++] = (GeoNavCell){.x = cell.x + 1, .y = cell.y};
  }
  if (LIKELY(cell.x >= 1)) {
    out[count++] = (GeoNavCell){.x = cell.x - 1, .y = cell.y};
  }
  if (LIKELY((u16)(cell.y + 1) < grid->cellCountAxis)) {
    out[count++] = (GeoNavCell){.x = cell.x, .y = cell.y + 1};
  }
  if (LIKELY(cell.y >= 1)) {
    out[count++] = (GeoNavCell){.x = cell.x, .y = cell.y - 1};
  }
  return count;
}

static bool nav_cell_add_occupant(GeoNavGrid* g, const u32 cellIndex, const u16 occupantIdx) {
  u16* occupancyItr = &g->cellOccupancy[cellIndex * geo_nav_occupants_per_cell];
  u16* occupancyEnd = occupancyItr + geo_nav_occupants_per_cell;
  do {
    if (sentinel_check(*occupancyItr)) {
      *occupancyItr = occupantIdx;
      return true;
    }
  } while (++occupancyItr != occupancyEnd);

  return false; // Maximum occupants per cell reached.
}

INLINE_HINT static GeoVector nav_cell_pos_no_y(const GeoNavGrid* grid, const GeoNavCell cell) {
  const GeoVector pos = geo_vector_mul(geo_vector(cell.x, 0, cell.y), grid->cellSize);
  return geo_vector_add(pos, grid->cellOffset);
}

INLINE_HINT static GeoVector nav_cell_pos(const GeoNavGrid* grid, const GeoNavCell cell) {
  GeoVector pos = geo_vector_mul(geo_vector(cell.x, 0, cell.y), grid->cellSize);
  pos.y         = grid->cellY[nav_cell_index(grid, cell)];
  return geo_vector_add(pos, grid->cellOffset);
}

static GeoBox nav_cell_box(const GeoNavGrid* grid, const GeoNavCell cell) {
  // Shrink by a tiny bit to avoid blockers that are touching a cell from immediately blocking it.
  static const f32 g_overlapEpsilon = 1e-4f;

  const GeoVector center       = nav_cell_pos(grid, cell);
  const f32       cellHalfSize = (grid->cellSize - g_overlapEpsilon) * 0.5f;
  return (GeoBox){
      .min = geo_vector_sub(center, geo_vector(cellHalfSize, 0, cellHalfSize)),
      .max = geo_vector_add(center, geo_vector(cellHalfSize, grid->cellHeight, cellHalfSize)),
  };
}

typedef enum {
  GeoNavMap_ClampedX = 1 << 0,
  GeoNavMap_ClampedY = 1 << 1,
} GeoNavMapFlags;

typedef struct {
  GeoNavCell     cell;
  GeoNavMapFlags flags;
} GeoNavMapResult;

INLINE_HINT static GeoNavMapResult nav_cell_map_local(const GeoNavGrid* grid, GeoVector local) {
  local = geo_vector_round_nearest(local);

  GeoNavMapFlags flags = 0;
  if (UNLIKELY(nav_cell_clamp_axis(grid, &local.x))) {
    flags |= GeoNavMap_ClampedX;
  }
  if (UNLIKELY(nav_cell_clamp_axis(grid, &local.z))) {
    flags |= GeoNavMap_ClampedY;
  }
  return (GeoNavMapResult){
      .cell  = {.x = (u16)local.x, .y = (u16)local.z},
      .flags = flags,
  };
}

INLINE_HINT static GeoNavMapResult nav_cell_map(const GeoNavGrid* grid, const GeoVector pos) {
  const GeoVector local = geo_vector_mul(geo_vector_sub(pos, grid->cellOffset), grid->cellDensity);
  return nav_cell_map_local(grid, local);
}

static GeoNavRegion nav_cell_map_box_local(const GeoNavGrid* grid, const GeoBox* localBox) {
  const GeoNavMapResult resMin = nav_cell_map_local(grid, localBox->min);
  GeoNavMapResult       resMax = nav_cell_map_local(grid, localBox->max);
  if (LIKELY((resMin.flags & resMax.flags & GeoNavMap_ClampedX) == 0)) {
    ++resMax.cell.x; // +1 because max is exclusive.
  }
  if (LIKELY((resMin.flags & resMax.flags & GeoNavMap_ClampedY) == 0)) {
    ++resMax.cell.y; // +1 because max is exclusive.
  }
  return (GeoNavRegion){.min = resMin.cell, .max = resMax.cell};
}

static GeoNavRegion nav_cell_map_box(const GeoNavGrid* grid, const GeoBox* box) {
  // Shrink by a tiny bit to avoid blockers that are touching a cell from immediately blocking it.
  static const GeoVector g_overlapEpsilon = {.x = -1e-4f, .z = -1e-4f};

  GeoBox localBox;
  localBox.min = geo_vector_mul(geo_vector_sub(box->min, grid->cellOffset), grid->cellDensity);
  localBox.max = geo_vector_mul(geo_vector_sub(box->max, grid->cellOffset), grid->cellDensity);
  localBox     = geo_box_dilate(&localBox, g_overlapEpsilon);

  return nav_cell_map_box_local(grid, &localBox);
}

static GeoNavRegion nav_cell_grow(const GeoNavGrid* grid, const GeoNavCell cell, const u16 radius) {
  const u16 minX = cell.x - nav_min_u16(cell.x, radius);
  const u16 minY = cell.y - nav_min_u16(cell.y, radius);
  const u16 maxX = nav_min_u16(cell.x + radius, grid->cellCountAxis - 1) + 1;
  const u16 maxY = nav_min_u16(cell.y + radius, grid->cellCountAxis - 1) + 1;
  return (GeoNavRegion){.min = {.x = minX, .y = minY}, .max = {.x = maxX, .y = maxY}};
}

static f32 nav_cell_dist_sqr(const GeoNavGrid* grid, const GeoNavCell cell, const GeoVector tgt) {
  const f32       cellRadiusAxis = grid->cellSize * 0.5f + f32_epsilon;
  const GeoVector cellRadius     = geo_vector(cellRadiusAxis, 0, cellRadiusAxis);
  const GeoVector cellPos        = nav_cell_pos_no_y(grid, cell);
  const GeoVector deltaMin       = geo_vector_sub(geo_vector_sub(cellPos, cellRadius), tgt);
  const GeoVector deltaMax       = geo_vector_sub(tgt, geo_vector_add(cellPos, cellRadius));
  const GeoVector delta = geo_vector_max(geo_vector_max(deltaMin, deltaMax), geo_vector(0));
  return geo_vector_mag_sqr(geo_vector_xz(delta));
}

static u16 nav_manhattan_dist(const GeoNavCell from, const GeoNavCell to) {
  const i16 diffX = to.x - (i16)from.x;
  const i16 diffY = to.y - (i16)from.y;
  return nav_abs_i16(diffX) + nav_abs_i16(diffY);
}

static u16 nav_chebyshev_dist(const GeoNavCell from, const GeoNavCell to) {
  const i16 diffX = to.x - (i16)from.x;
  const i16 diffY = to.y - (i16)from.y;
  return math_max(nav_abs_i16(diffX), nav_abs_i16(diffY));
}

static u16 nav_path_heuristic(const GeoNavCell from, const GeoNavCell to) {
  /**
   * Basic distance to estimate the cost between the two cells.
   * Additionally we add a multiplier to make the A* search more greedy to reduce the amount of
   * visited cells with the trade-off of less optimal paths.
   *
   * Using the Chebyshev distance requires more cells to be visited but will result in paths that
   * are visually more pleasing in our use-case as the units can move diagonally.
   */
  enum { ExpectedCostPerCell = 1, Greediness = 2 };
#if geo_nav_path_chebyshev_heuristic
  return nav_chebyshev_dist(from, to) * ExpectedCostPerCell * Greediness;
#else
  return nav_manhattan_dist(from, to) * ExpectedCostPerCell * Greediness;
#endif
}

static u16 nav_path_cost(const GeoNavGrid* g, const u32 cellIndex) {
  enum { NormalCost = 1, OccupiedStationaryCost = 10 };

  if (nav_bit_test(g->cellOccupiedStationarySet, cellIndex)) {
    return OccupiedStationaryCost;
  }
  return NormalCost;
}

typedef struct {
  GeoNavCell cells[geo_nav_path_queue_size];
  u16        costs[geo_nav_path_queue_size];
  u32        count;
} NavPathQueue;

static bool path_queue_empty(NavPathQueue* q) { return q->count == 0; }
static bool path_queue_full(NavPathQueue* q) { return q->count == geo_nav_path_queue_size; }

/**
 * Remove the lowest cost cell from the queue.
 * Pre-condition: Queue not empty.
 */
static GeoNavCell path_queue_pop(NavPathQueue* q) { return q->cells[--q->count]; }

/**
 * Insert a cell into the queue at the end.
 * NOTE: Does not preserve cost sorting.
 * Pre-condition: Queue not full.
 */
static void path_queue_append(NavPathQueue* q, const GeoNavCell cell, const u16 cost) {
  q->cells[q->count] = cell;
  q->costs[q->count] = cost;
  ++q->count;
}

/**
 * Insert a cell into the queue at the given index.
 * NOTE: Does not preserve cost sorting.
 * Pre-condition: Queue not full.
 */
static void path_queue_insert(NavPathQueue* q, const GeoNavCell cell, const u16 cost, const u32 i) {
  const GeoNavCell* cellsItr = &q->cells[i];
  const GeoNavCell* cellsEnd = &q->cells[q->count];
  mem_move(mem_from_to(cellsItr + 1, cellsEnd + 1), mem_from_to(cellsItr, cellsEnd));

  const u16* prioItr = &q->costs[i];
  const u16* prioEnd = &q->costs[q->count];
  mem_move(mem_from_to(prioItr + 1, prioEnd + 1), mem_from_to(prioItr, prioEnd));

  q->cells[i] = cell;
  q->costs[i] = cost;
  ++q->count;
}

/**
 * Insert the given cell into the queue sorted on cost.
 * Pre-condition: Cell does not exist in the queue yet.
 * Pre-condition: Queue not full.
 */
static void path_queue_push(NavPathQueue* q, const GeoNavCell cell, const u16 cost) {
  /**
   * Binary search to find the first entry with a lower cost and insert before it.
   */
  u32 itr = 0;
  u32 rem = q->count;
  while (rem) {
    const u32 step   = rem / 2;
    const u32 middle = itr + step;
    if (cost <= q->costs[middle]) {
      itr = middle + 1;
      rem -= step + 1;
    } else {
      rem = step;
    }
  }
  if (itr == q->count) {
    // No lower cost found; insert it at the end.
    path_queue_append(q, cell, cost);
  } else {
    // Cost at itr was lower; insert it before itr.
    path_queue_insert(q, cell, cost, itr);
  }
}

static bool
nav_path(const GeoNavGrid* grid, GeoNavWorkerState* s, const GeoNavCell from, const GeoNavCell to) {
  mem_set(s->markedCells, 0);
  mem_set(mem_create(s->costs, grid->cellCountTotal * sizeof(u16)), 0xFF);

  ++s->stats[GeoNavStat_PathCount];       // Track amount of path queries.
  ++s->stats[GeoNavStat_PathItrEnqueues]; // Include the initial enqueue in the tracking.

  s->costs[nav_cell_index(grid, from)] = 0;

  NavPathQueue queue;
  queue.count = 0; // NOTE: No need to clear the whole queue but count needs to be initialized.
  path_queue_append(&queue, from, nav_path_heuristic(from, to));

  u32 iterations = 0;
  while (!path_queue_empty(&queue)) {
    ++s->stats[GeoNavStat_PathItrCells]; // Track total amount of path iterations.

    if (++iterations > geo_nav_path_iterations_max) {
      ++s->stats[GeoNavStat_PathLimiterCount];
      break; // Finding a path to destination takes too many iterations; treat it as unreachable.
    }

    const GeoNavCell cell      = path_queue_pop(&queue);
    const u32        cellIndex = nav_cell_index(grid, cell);
    if (cell.data == to.data) {
      return true; // Destination reached.
    }
    nav_bit_clear(s->markedCells, cellIndex);

    GeoNavCell neighbors[4];
    const u32  neighborCount = nav_cell_neighbors(grid, cell, neighbors);
    for (u32 i = 0; i != neighborCount; ++i) {
      const GeoNavCell neighbor      = neighbors[i];
      const u32        neighborIndex = nav_cell_index(grid, neighbor);
      if (grid->cellBlockerCount[neighborIndex]) {
        continue; // Ignore blocked cells;
      }
      const u16 tentativeCost = s->costs[cellIndex] + nav_path_cost(grid, neighborIndex);
      if (tentativeCost < s->costs[neighborIndex]) {
        /**
         * This path to the neighbor is better then the previous, record it and enqueue the neighbor
         * for rechecking.
         */
        s->cameFrom[neighborIndex] = cell;
        s->costs[neighborIndex]    = tentativeCost;

        const u16 expectedCostToGoal = tentativeCost + nav_path_heuristic(neighbor, to);
        if (!nav_bit_test(s->markedCells, neighborIndex)) {
          /**
           * Enqueue the neighbor to be checked.
           * NOTE: If the queue is full we skip the neighbor instead of bailing; reason is we could
           * still find a valid path with the currently queued cells.
           */
          if (!path_queue_full(&queue)) {
            ++s->stats[GeoNavStat_PathItrEnqueues]; // Track total amount of path cell enqueues.
            path_queue_push(&queue, neighbor, expectedCostToGoal);
          }
          nav_bit_set(s->markedCells, neighborIndex);
        }
      }
    }
  }
  return false; // Destination unreachable.
}

/**
 * Compute the count of cells in the output path.
 * NOTE: Only valid if a valid path has been found between the cells using 'nav_path'.
 */
static u32 nav_path_output_count(
    const GeoNavGrid* grid, GeoNavWorkerState* s, const GeoNavCell from, const GeoNavCell to) {
  /**
   * Walk the cameFrom chain backwards starting from 'from' until we reach 'to' and count the
   * number of cells in the path.
   */
  u32 count = 1;
  for (GeoNavCell itr = to; itr.data != from.data; ++count) {
    itr = s->cameFrom[nav_cell_index(grid, itr)];
  }
  return count;
}

/**
 * Write the computed path to the output container.
 * NOTE: Only valid if a valid path has been found between the cells using 'nav_path'.
 */
static u32 nav_path_output(
    const GeoNavGrid*         grid,
    GeoNavWorkerState*        s,
    const GeoNavCell          from,
    const GeoNavCell          to,
    const GeoNavCellContainer out) {
  /**
   * Reverse the path by first counting the total amount of cells and then inserting starting form
   * the end.
   */
  const u32 count = nav_path_output_count(grid, s, from, to);
  u32       i     = 1;

  ++s->stats[GeoNavStat_PathOutputCells]; // Track the total amount of output cells
  if (out.capacity > (count - i)) {
    out.cells[count - i] = to;
  }

  for (GeoNavCell itr = to; itr.data != from.data; ++i) {
    ++s->stats[GeoNavStat_PathOutputCells]; // Track the total amount of output cells.

    itr = s->cameFrom[nav_cell_index(grid, itr)];
    if (out.capacity > (count - 1 - i)) {
      out.cells[count - 1 - i] = itr;
    }
  }
  return math_min(count, out.capacity);
}

/**
 * Breadth-first search for N cells matching the given predicate.
 */
static u32 nav_find(
    const GeoNavGrid*   grid,
    GeoNavWorkerState*  s,
    const void*         ctx,
    const GeoNavCell    from,
    NavCellPredicate    predicate,
    GeoNavCellContainer out) {
  diag_assert(out.capacity);

  ++s->stats[GeoNavStat_FindCount];       // Track amount of find queries.
  ++s->stats[GeoNavStat_FindItrEnqueues]; // Include the initial enqueue in the tracking.

  GeoNavCell queue[512];
  u32        queueStart = 0;
  u32        queueEnd   = 0;

  // Insert the first cell.
  queue[0] = from;
  queueEnd = 1;

  mem_set(s->markedCells, 0);
  nav_bit_set(s->markedCells, nav_cell_index(grid, from));

  u32 outCount = 0;
  while (queueStart != queueEnd) {
    ++s->stats[GeoNavStat_FindItrCells]; // Track total amount of find iterations.

    const GeoNavCell cell      = queue[queueStart++];
    const u32        cellIndex = nav_cell_index(grid, cell);
    if (predicate(grid, ctx, cellIndex)) {
      out.cells[outCount++] = cell;
      if (outCount == out.capacity) {
        return outCount; // Filed the entire output.
      }
    }

    GeoNavCell neighbors[4];
    const u32  neighborCount = nav_cell_neighbors(grid, cell, neighbors);
    for (u32 i = 0; i != neighborCount; ++i) {
      const GeoNavCell neighbor      = neighbors[i];
      const u32        neighborIndex = nav_cell_index(grid, neighbor);
      if (nav_bit_test(s->markedCells, neighborIndex)) {
        continue;
      }
      if (queueEnd == array_elems(queue)) {
        // Queue exhausted; reclaim the unused space at the beginning of the queue.
        mem_move(array_mem(queue), mem_from_to(queue + queueStart, queue + queueEnd));
        queueEnd -= queueStart;
        queueStart = 0;
        if (UNLIKELY(queueEnd == array_elems(queue))) {
          log_e("Find queue was not big enough to satisfy request");
          return outCount;
        }
      }
      ++s->stats[GeoNavStat_FindItrEnqueues]; // Track total amount of find cell enqueues.
      queue[queueEnd++] = neighbor;
      nav_bit_set(s->markedCells, neighborIndex);
    }
  }
  return outCount;
}

static bool nav_pred_blocked(const GeoNavGrid* g, const void* ctx, const u32 cellIndex) {
  (void)ctx;
  return g->cellBlockerCount[cellIndex] != 0;
}

static bool nav_pred_unblocked(const GeoNavGrid* g, const void* ctx, const u32 cellIndex) {
  (void)ctx;
  return g->cellBlockerCount[cellIndex] == 0;
}

static bool nav_pred_occupied(const GeoNavGrid* g, const void* ctx, const u32 cellIndex) {
  (void)ctx;
  const u16* occupancyItr = &g->cellOccupancy[cellIndex * geo_nav_occupants_per_cell];
  const u16* occupancyEnd = occupancyItr + geo_nav_occupants_per_cell;
  do {
    if (!sentinel_check(*occupancyItr)) {
      return true; // Occupant found.
    }
  } while (++occupancyItr != occupancyEnd);

  return false;
}

static bool
nav_pred_occupied_stationary(const GeoNavGrid* g, const void* ctx, const u32 cellIndex) {
  (void)ctx;
  return nav_bit_test(g->cellOccupiedStationarySet, cellIndex);
}

static bool nav_pred_occupied_moving(const GeoNavGrid* g, const void* ctx, const u32 cellIndex) {
  (void)ctx;
  /**
   * Test if the cell has a moving occupant.
   */
  const u16* occupancyItr = &g->cellOccupancy[cellIndex * geo_nav_occupants_per_cell];
  const u16* occupancyEnd = occupancyItr + geo_nav_occupants_per_cell;
  do {
    const u16 occupantIndex = *occupancyItr;
    if (sentinel_check(occupantIndex)) {
      continue; // Cell occupant slot empty.
    }
    if (g->occupants[occupantIndex].flags & GeoNavOccupantFlags_Moving) {
      return true; // Cell has a moving occupant.
    }
  } while (++occupancyItr != occupancyEnd);

  return false;
}

static bool nav_pred_free(const GeoNavGrid* g, const void* ctx, const u32 cellIndex) {
  (void)ctx;
  /**
   * Test if the cell is not blocked and has no stationary occupant.
   */
  return !g->cellBlockerCount[cellIndex] && !nav_bit_test(g->cellOccupiedStationarySet, cellIndex);
}

static bool nav_pred_non_free(const GeoNavGrid* g, const void* ctx, const u32 cellIndex) {
  (void)ctx;
  /**
   * Test if the cell is blocked or has a stationary occupant.
   */
  return g->cellBlockerCount[cellIndex] || nav_bit_test(g->cellOccupiedStationarySet, cellIndex);
}

static bool nav_pred_reachable(const GeoNavGrid* g, const void* ctx, const u32 cellIndex) {
  const GeoNavIsland* refIsland = ctx;
  return g->cellIslands[cellIndex] == *refIsland;
}

static const NavCellPredicate g_geoNavPredCond[] = {
    [GeoNavCond_Blocked]            = nav_pred_blocked,
    [GeoNavCond_Unblocked]          = nav_pred_unblocked,
    [GeoNavCond_Occupied]           = nav_pred_occupied,
    [GeoNavCond_OccupiedStationary] = nav_pred_occupied_stationary,
    [GeoNavCond_OccupiedMoving]     = nav_pred_occupied_moving,
    [GeoNavCond_Free]               = nav_pred_free,
    [GeoNavCond_NonFree]            = nav_pred_non_free,
};

static bool nav_pred_condition(const GeoNavGrid* g, const void* ctx, const u32 cellIndex) {
  const GeoNavCond* cond = ctx;
  return g_geoNavPredCond[*cond](g, ctx, cellIndex);
}

/**
 * Get all the occupants in the given region.
 * Returns the amount of occupants written to the out array.
 * NOTE: Array size should be at least 'nav_region_size(region) * geo_nav_occupants_per_cell'.
 */
static u32
nav_region_occupants(const GeoNavGrid* g, const GeoNavRegion region, const GeoNavOccupant** out) {
  const u32 occupantsHorizontal = (region.max.x - region.min.x) * geo_nav_occupants_per_cell;
  if (!occupantsHorizontal) {
    return 0;
  }
  const GeoNavOccupant** outStart = out;
  for (u16 y = region.min.y; y != region.max.y; ++y) {
    const u32  cellIndex    = nav_cell_index(g, (GeoNavCell){.x = region.min.x, .y = y});
    const u16* occupancyItr = &g->cellOccupancy[cellIndex * geo_nav_occupants_per_cell];
    const u16* occupancyEnd = occupancyItr + occupantsHorizontal;
    do {
      const u16 occupantIndex = *occupancyItr;
      if (!sentinel_check(occupantIndex)) {
        *out++ = &g->occupants[occupantIndex];
      }
    } while (++occupancyItr != occupancyEnd);
  }
  return (u32)(out - outStart);
}

/**
 * Compute a vector that pushes away from any blockers in the region.
 * NOTE: Behavior is undefined if the position is fully inside a blocked cell.
 */
static GeoVector
nav_separate_from_blockers(const GeoNavGrid* grid, const GeoNavRegion reg, const GeoVector pos) {
  const f32 reqDist    = grid->cellSize * geo_nav_channel_radius_frac;
  const f32 reqDistSqr = reqDist * reqDist;

  GeoVector result = {0};
  for (u16 y = reg.min.y; y != reg.max.y; ++y) {
    u32 cellIndex = nav_cell_index(grid, (GeoNavCell){.x = reg.min.x, .y = y});
    for (u16 x = reg.min.x; x != reg.max.x; ++x, ++cellIndex) {
      if (grid->cellBlockerCount[cellIndex] == 0) {
        continue; // Cell not blocked.
      }
      const GeoNavCell cell          = {.x = x, .y = y};
      const f32        distToEdgeSqr = nav_cell_dist_sqr(grid, cell, pos);
      if (distToEdgeSqr >= reqDistSqr) {
        continue; // Far enough away.
      }
      const f32       distToEdge = intrinsic_sqrt_f32(distToEdgeSqr);
      const f32       overlap    = reqDist - distToEdge;
      const GeoVector cellPos    = nav_cell_pos_no_y(grid, cell);
      const GeoVector sepDir     = geo_vector_norm(geo_vector_xz(geo_vector_sub(pos, cellPos)));
      result                     = geo_vector_add(result, geo_vector_mul(sepDir, overlap));
    }
  }
  return result;
}

/**
 * Compute a vector to move an occupant to be at least radius away any other occupant.
 * NOTE: id can be used to ignore an existing occupant (for example itself).
 * Pre-condition: nav_region_size(region) <= 9.
 */
static GeoVector nav_separate_from_occupied(
    const GeoNavGrid*  grid,
    const GeoNavRegion region,
    const u64          userId,
    const GeoVector    pos,
    const f32          radius,
    const f32          weight) {
  const GeoNavOccupant* occupants[(3 * 3) * geo_nav_occupants_per_cell];
  diag_assert((nav_region_size(region) * geo_nav_occupants_per_cell) <= array_elems(occupants));

  const u32 occupantCount = nav_region_occupants(grid, region, occupants);

  GeoVector result = {0};
  for (u32 i = 0; i != occupantCount; ++i) {
    const GeoNavOccupant* occupant = occupants[i];
    if (occupant->userId == userId) {
      continue; // Ignore occupants with the same userId.
    }
    const GeoVector toOccupant = geo_vector(occupant->pos[0] - pos.x, 0, occupant->pos[1] - pos.z);
    const f32       distSqr    = geo_vector_mag_sqr(toOccupant);
    const f32       sepDist    = occupant->radius + radius;
    if (distSqr >= (sepDist * sepDist)) {
      continue; // Far enough away.
    }
    const f32 dist = intrinsic_sqrt_f32(distSqr);
    GeoVector sepDir;
    if (UNLIKELY(dist < 1e-4f)) {
      // Occupants occupy the same position; pick a random direction.
      const GeoQuat rot = geo_quat_angle_axis(rng_sample_f32(g_rng) * math_pi_f32 * 2.0f, geo_up);
      sepDir            = geo_quat_rotate(rot, geo_forward);
    } else {
      sepDir = geo_vector_div(toOccupant, dist);
    }
    const f32 otherWeight = occupant->weight;
    const f32 relWeight   = otherWeight / (weight + otherWeight);

    // NOTE: Times 0.5 because both occupants are expected to move.
    // NOTE: sepStrength will be negative to push away instead of towards.
    const f32 sepStrength = (dist - sepDist) * 0.5f * relWeight;
    result                = geo_vector_add(result, geo_vector_mul(sepDir, sepStrength));
  }
  result.y = 0; // Zero out any movement out of the grid's plane.
  return result;
}

INLINE_HINT static void nav_cell_block(GeoNavGrid* grid, const u32 cellIndex) {
  diag_assert_msg(grid->cellBlockerCount[cellIndex] != u8_max, "Cell blocked count exceeds max");
  ++grid->cellBlockerCount[cellIndex];
}

INLINE_HINT static bool nav_cell_unblock(GeoNavGrid* grid, const u32 cellIndex) {
  diag_assert_msg(grid->cellBlockerCount[cellIndex], "Cell not currently blocked");
  return --grid->cellBlockerCount[cellIndex] == 0;
}

static u32 nav_blocker_count(GeoNavGrid* grid) {
  return (u32)(geo_nav_blockers_max - bitset_count(grid->blockerFreeSet));
}

static GeoNavBlockerId nav_blocker_acquire(GeoNavGrid* grid) {
  const usize index = bitset_next(grid->blockerFreeSet, 0);
  if (UNLIKELY(sentinel_check(index))) {
    log_e("Navigation blocker limit reached", log_param("limit", fmt_int(geo_nav_blockers_max)));
    return geo_blocker_invalid;
  }
  nav_bit_clear(grid->blockerFreeSet, (u32)index);
  return (GeoNavBlockerId)index;
}

static bool nav_blocker_release(GeoNavGrid* grid, const GeoNavBlockerId blockerId) {
  diag_assert_msg(!nav_bit_test(grid->blockerFreeSet, blockerId), "Blocker double free");

  const GeoNavBlocker* blocker         = &grid->blockers[blockerId];
  const GeoNavRegion   region          = blocker->region;
  const BitSet         blockedInRegion = bitset_from_array(blocker->blockedInRegion);

  bool anyBecameUnblocked = false;

  u32 indexInRegion = 0;
  for (u16 y = region.min.y; y != region.max.y; ++y) {
    u32 cellIndex = nav_cell_index(grid, (GeoNavCell){.x = region.min.x, .y = y});
    for (u16 x = region.min.x; x != region.max.x; ++x, ++cellIndex) {
      if (nav_bit_test(blockedInRegion, indexInRegion)) {
        anyBecameUnblocked |= nav_cell_unblock(grid, cellIndex);
      }
      ++indexInRegion;
    }
  }
  nav_bit_set(grid->blockerFreeSet, blockerId);
  return anyBecameUnblocked;
}

static bool nav_blocker_release_all(GeoNavGrid* grid) {
  if (nav_blocker_count(grid) != 0) {
    bitset_set_all(grid->blockerFreeSet, geo_nav_blockers_max); // All blockers free again.
    mem_set(mem_create(grid->cellBlockerCount, sizeof(u8) * grid->cellCountTotal), 0);
    return true;
  }
  return false;
}

static bool nav_blocker_neighbors_island(
    const GeoNavGrid* grid, const GeoNavBlockerId blockerId, const GeoNavIsland island) {

  const GeoNavBlocker* blocker         = &grid->blockers[blockerId];
  const GeoNavRegion   region          = blocker->region;
  const BitSet         blockedInRegion = bitset_from_array(blocker->blockedInRegion);

  u32 indexInRegion = 0;
  for (u16 y = region.min.y; y != region.max.y; ++y) {
    for (u16 x = region.min.x; x != region.max.x; ++x) {
      if (nav_bit_test(blockedInRegion, indexInRegion)) {
        const GeoNavCell cell = {.x = x, .y = y};

        // Test if any neighbor belongs to the given island.
        GeoNavCell neighbors[4];
        const u32  neighborCount = nav_cell_neighbors(grid, cell, neighbors);
        for (u32 i = 0; i != neighborCount; ++i) {
          if (grid->cellIslands[nav_cell_index(grid, neighbors[i])] == island) {
            return true;
          }
        }
      }
      ++indexInRegion;
    }
  }
  return false;
}

static GeoNavCell nav_blocker_closest_reachable(
    const GeoNavGrid* grid, const GeoNavBlockerId blockerId, const GeoNavCell from) {

  const GeoNavBlocker* blocker         = &grid->blockers[blockerId];
  const GeoNavRegion   region          = blocker->region;
  const BitSet         blockedInRegion = bitset_from_array(blocker->blockedInRegion);
  const u32            fromCellIndex   = nav_cell_index(grid, from);
  const GeoNavIsland   fromIsland      = nav_island(grid, fromCellIndex);

  GeoNavCell bestCell      = from;
  u16        bestCost      = u16_max;
  u32        indexInRegion = 0;
  for (u16 y = region.min.y; y != region.max.y; ++y) {
    for (u16 x = region.min.x; x != region.max.x; ++x) {
      if (nav_bit_test(blockedInRegion, indexInRegion)) {
        const GeoNavCell cell = {.x = x, .y = y};

        // Find a neighbor with the lowest cost thats in the same island as 'from'
        GeoNavCell neighbors[4];
        const u32  neighborCount = nav_cell_neighbors(grid, cell, neighbors);
        for (u32 i = 0; i != neighborCount; ++i) {
          if (grid->cellIslands[nav_cell_index(grid, neighbors[i])] != fromIsland) {
            continue; // Can't reach 'from'.
          }
          const u16 cost = nav_path_heuristic(from, neighbors[i]);
          if (cost < bestCost) {
            bestCell = neighbors[i];
            bestCost = cost;
          }
        }
      }
      ++indexInRegion;
    }
  }
  return bestCell;
}

static void nav_island_queue_clear(GeoNavIslandUpdater* u) { u->queueStart = u->queueEnd = 0; }
static bool nav_island_queue_empty(GeoNavIslandUpdater* u) { return u->queueStart == u->queueEnd; }
static GeoNavCell nav_island_queue_pop(GeoNavIslandUpdater* u) { return u->queue[u->queueStart++]; }

static void nav_island_queue_push(GeoNavIslandUpdater* u, const GeoNavCell cell) {
  if (UNLIKELY(u->queueEnd == array_elems(u->queue))) {
    // Queue exhausted; reclaim the unused space at the beginning of the queue.
    mem_move(array_mem(u->queue), mem_from_to(u->queue + u->queueStart, u->queue + u->queueEnd));
    u->queueEnd -= u->queueStart;
    u->queueStart = 0;

    if (UNLIKELY(u->queueEnd == array_elems(u->queue))) {
      diag_crash_msg("Queue exhausted while filling navigation island");
    }
  }
  u->queue[u->queueEnd++] = cell;
}

typedef enum {
  NavIslandUpdate_Done,
  NavIslandUpdate_Busy,
} NavIslandUpdateResult;

static NavIslandUpdateResult nav_island_queue_update(GeoNavIslandUpdater* u, GeoNavGrid* grid) {
  diag_assert(!nav_island_queue_empty(u));

  // Flood-fill to all unblocked neighbors.
  do {
    if (UNLIKELY(++u->currentItr > geo_nav_island_itr_per_tick)) {
      return NavIslandUpdate_Busy;
    }
    const GeoNavCell cell = nav_island_queue_pop(u);

    GeoNavCell neighbors[4];
    const u32  neighborCount = nav_cell_neighbors(grid, cell, neighbors);
    for (u32 i = 0; i != neighborCount; ++i) {
      const GeoNavCell neighbor      = neighbors[i];
      const u32        neighborIndex = nav_cell_index(grid, neighbor);
      if (nav_bit_test(u->markedCells, neighborIndex)) {
        continue; // Cell already processed.
      }
      if (grid->cellBlockerCount[neighborIndex] != 0) {
        continue; // Neighbor blocked.
      }
      grid->cellIslands[neighborIndex] = u->currentIsland;
      nav_bit_set(u->markedCells, neighborIndex);
      nav_island_queue_push(u, neighbor);
    }
  } while (!nav_island_queue_empty(u));

  return NavIslandUpdate_Done;
}

static void nav_island_update_start(GeoNavGrid* grid) {
  GeoNavIslandUpdater* u = &grid->islandUpdater;
  diag_assert((u->flags & GeoNavIslandUpdater_Active) == 0);
  diag_assert((u->flags & GeoNavIslandUpdater_Dirty) != 0);
  diag_assert(nav_island_queue_empty(u));

  u->flags |= GeoNavIslandUpdater_Active;
  u->flags &= ~GeoNavIslandUpdater_Dirty;

  u->currentIsland  = 0;
  u->currentRegionY = geo_nav_bounds(grid).min.y;
  mem_set(u->markedCells, 0);
}

static void nav_island_update_stop(GeoNavGrid* grid) {
  GeoNavIslandUpdater* u = &grid->islandUpdater;
  diag_assert((u->flags & GeoNavIslandUpdater_Active) != 0);
  diag_assert(nav_island_queue_empty(u));

  u->flags &= ~GeoNavIslandUpdater_Active;
  grid->islandCount = u->currentIsland;
}

static NavIslandUpdateResult nav_island_update_tick(GeoNavGrid* grid) {
  GeoNavIslandUpdater* u = &grid->islandUpdater;
  diag_assert((u->flags & GeoNavIslandUpdater_Active) != 0);

  ++grid->stats[GeoNavStat_IslandComputes]; // Track island computes.
  u->currentItr = 0;                        // Reset the 'per frame' interation counter.

  /**
   * Assign an island to each cell. For each non-processed cell we start a flood fill that assigns
   * the same island to each of its unblocked neighbors. A flood fill can take multiple ticks to
   * finish due to the 'geo_nav_island_itr_per_tick' limit on the amount of iterations per tick.
   */

  // If there's a flood-fill active then keep processing it.
  if (!nav_island_queue_empty(u)) {
    if (nav_island_queue_update(u, grid) != NavIslandUpdate_Done) {
      return NavIslandUpdate_Busy;
    }
    ++u->currentIsland;
  }

  // If not; start a new flood-fill for the first non-processed cell.
  const GeoNavRegion region = geo_nav_bounds(grid);
  for (; u->currentRegionY != region.max.y; ++u->currentRegionY) {
    u32 cellIndex = nav_cell_index(grid, (GeoNavCell){.x = region.min.x, .y = u->currentRegionY});
    for (u16 x = region.min.x; x != region.max.x; ++x, ++cellIndex) {
      if (nav_bit_test(u->markedCells, cellIndex)) {
        continue; // Cell already processed.
      }
      if (grid->cellBlockerCount[cellIndex] != 0) {
        // Assign it to the 'blocked' island.
        grid->cellIslands[cellIndex] = geo_nav_island_blocked;
        nav_bit_set(u->markedCells, cellIndex);
        continue;
      }
      if (u->currentIsland == geo_nav_island_max) {
        log_w("Navigation island limit reached", log_param("limit", fmt_int(geo_nav_island_max)));
        return NavIslandUpdate_Done;
      }
      const GeoNavCell cell = {.x = x, .y = u->currentRegionY};

      // Assign the starting cell to the island.
      grid->cellIslands[cellIndex] = u->currentIsland;
      nav_bit_set(u->markedCells, cellIndex);

      // And flood-fill its unblocked neighbors.
      nav_island_queue_clear(u);
      nav_island_queue_push(u, cell);
      if (nav_island_queue_update(u, grid) != NavIslandUpdate_Done) {
        return NavIslandUpdate_Busy;
      }
      ++u->currentIsland;
    }
  }

  // All cells have been processed.
  return NavIslandUpdate_Done;
}

GeoNavGrid* geo_nav_grid_create(
    Allocator* alloc, const f32 size, const f32 cellSize, const f32 height, const f32 blockHeight) {
  diag_assert(size > 1e-4f && size < 1e4f);
  diag_assert(cellSize > 1e-4f && cellSize < 1e4f);
  diag_assert(height > 1e-4f);
  diag_assert(blockHeight > 1e-4f);

  GeoNavGrid* grid = alloc_alloc_t(alloc, GeoNavGrid);

  u32 cellCountAxis = (u32)math_round_nearest_f32(size / cellSize);
  cellCountAxis += !(cellCountAxis % 2); // Align to be odd (so there's always a center cell).

  const u32 cellCountTotal = cellCountAxis * cellCountAxis;

  *grid = (GeoNavGrid){
      .size             = size,
      .cellCountAxis    = cellCountAxis,
      .cellCountTotal   = cellCountTotal,
      .cellSize         = cellSize,
      .cellDensity      = 1.0f / cellSize,
      .cellHeight       = height,
      .cellBlockHeight  = blockHeight,
      .cellOffset       = geo_vector(size * -0.5f, 0, size * -0.5f),
      .cellY            = alloc_array_t(alloc, f32, cellCountTotal),
      .cellBlockerCount = alloc_array_t(alloc, u8, cellCountTotal),
      .cellOccupancy    = alloc_array_t(alloc, u16, cellCountTotal * geo_nav_occupants_per_cell),
      .cellOccupiedStationarySet = alloc_alloc(alloc, bits_to_bytes(cellCountTotal) + 1, 1),
      .cellIslands               = alloc_array_t(alloc, GeoNavIsland, cellCountTotal),
      .blockers                  = alloc_array_t(alloc, GeoNavBlocker, geo_nav_blockers_max),
      .blockerFreeSet            = alloc_alloc(alloc, bits_to_bytes(geo_nav_blockers_max), 1),
      .occupants                 = alloc_array_t(alloc, GeoNavOccupant, geo_nav_occupants_max),
      .islandUpdater = {.markedCells = alloc_alloc(alloc, bits_to_bytes(cellCountTotal) + 1, 1)},
      .alloc         = alloc,
  };

  // Initialize cell y's and islands to 0.
  mem_set(mem_create(grid->cellY, sizeof(f32) * grid->cellCountTotal), 0);
  mem_set(mem_create(grid->cellIslands, sizeof(GeoNavIsland) * grid->cellCountTotal), 0);

  nav_blocker_release_all(grid);

  // Initialize worker state.
  diag_assert(g_jobsWorkerCount <= geo_nav_workers_max);
  for (u16 workerId = 0; workerId != g_jobsWorkerCount; ++workerId) {
    grid->workerStates[workerId] = nav_worker_state_create(grid);
  }

  return grid;
}

void geo_nav_grid_destroy(GeoNavGrid* grid) {
  alloc_free_array_t(grid->alloc, grid->cellY, grid->cellCountTotal);
  alloc_free_array_t(grid->alloc, grid->cellBlockerCount, grid->cellCountTotal);
  alloc_free_array_t(grid->alloc, grid->cellIslands, grid->cellCountTotal);
  alloc_free(grid->alloc, nav_occupancy_mem(grid));
  alloc_free_array_t(grid->alloc, grid->blockers, geo_nav_blockers_max);
  alloc_free(grid->alloc, grid->blockerFreeSet);
  alloc_free_array_t(grid->alloc, grid->occupants, geo_nav_occupants_max);
  alloc_free(grid->alloc, grid->cellOccupiedStationarySet);
  alloc_free(grid->alloc, grid->islandUpdater.markedCells);

  for (u32 i = 0; i != geo_nav_workers_max; ++i) {
    GeoNavWorkerState* state = grid->workerStates[i];
    if (state) {
      alloc_free(grid->alloc, state->markedCells);
      alloc_free_array_t(grid->alloc, state->costs, grid->cellCountTotal);
      alloc_free_array_t(grid->alloc, state->cameFrom, grid->cellCountTotal);
      alloc_free_t(grid->alloc, state);
    }
  }

  alloc_free_t(grid->alloc, grid);
}

GeoNavRegion geo_nav_bounds(const GeoNavGrid* grid) {
  return (GeoNavRegion){.max = {.x = grid->cellCountAxis, .y = grid->cellCountAxis}};
}

f32 geo_nav_size(const GeoNavGrid* grid) { return grid->size; }
f32 geo_nav_cell_size(const GeoNavGrid* grid) { return grid->cellSize; }

f32 geo_nav_channel_radius(const GeoNavGrid* grid) {
  return grid->cellSize * geo_nav_channel_radius_frac;
}

void geo_nav_y_update(GeoNavGrid* grid, const GeoNavCell cell, const f32 y) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);

  const u32  cellIndex   = nav_cell_index(grid, cell);
  const bool wasBlocked  = grid->cellY[cellIndex] >= grid->cellBlockHeight;
  const bool shouldBlock = y >= grid->cellBlockHeight;

  // Update y.
  grid->cellY[cellIndex] = y;

  // Updated blocked state.
  if (wasBlocked && !shouldBlock) {
    nav_cell_unblock(grid, cellIndex);
  } else if (!wasBlocked && shouldBlock) {
    nav_cell_block(grid, cellIndex);
  }
}

void geo_nav_y_clear(GeoNavGrid* grid) {
  for (u32 cellIndex = 0; cellIndex != grid->cellCountTotal; ++cellIndex) {
    const bool wasBlocked = grid->cellY[cellIndex] >= grid->cellBlockHeight;

    // Update y.
    grid->cellY[cellIndex] = 0.0f;

    // Clear blocked state.
    if (wasBlocked) {
      diag_assert_msg(grid->cellBlockerCount[cellIndex], "Expected the cell to be blocked");
      --grid->cellBlockerCount[cellIndex];
    }
  }
}

u16 geo_nav_manhattan_dist(const GeoNavGrid* grid, const GeoNavCell from, const GeoNavCell to) {
  (void)grid;
  diag_assert(from.x < grid->cellCountAxis && from.y < grid->cellCountAxis);
  diag_assert(to.x < grid->cellCountAxis && to.y < grid->cellCountAxis);

  return nav_manhattan_dist(from, to);
}

u16 geo_nav_chebyshev_dist(const GeoNavGrid* grid, const GeoNavCell from, const GeoNavCell to) {
  (void)grid;
  diag_assert(from.x < grid->cellCountAxis && from.y < grid->cellCountAxis);
  diag_assert(to.x < grid->cellCountAxis && to.y < grid->cellCountAxis);

  return nav_chebyshev_dist(from, to);
}

GeoVector geo_nav_position(const GeoNavGrid* grid, const GeoNavCell cell) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);
  return nav_cell_pos(grid, cell);
}

GeoNavCell geo_nav_at_position(const GeoNavGrid* grid, const GeoVector pos) {
  return nav_cell_map(grid, pos).cell;
}

GeoNavIsland geo_nav_island(const GeoNavGrid* grid, const GeoNavCell cell) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);

  const u32 cellIndex = nav_cell_index(grid, cell);

  return nav_island(grid, cellIndex);
}

bool geo_nav_reachable(const GeoNavGrid* grid, const GeoNavCell from, const GeoNavCell to) {
  diag_assert(from.x < grid->cellCountAxis && from.y < grid->cellCountAxis);
  diag_assert(to.x < grid->cellCountAxis && to.y < grid->cellCountAxis);

  const u32 fromCellIndex = nav_cell_index(grid, from);
  const u32 toCellIndex   = nav_cell_index(grid, to);

  return nav_island(grid, fromCellIndex) == nav_island(grid, toCellIndex);
}

bool geo_nav_check(const GeoNavGrid* grid, const GeoNavCell cell, const GeoNavCond cond) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);
  const u32 cellIndex = nav_cell_index(grid, cell);
  return g_geoNavPredCond[cond](grid, null, cellIndex);
}

bool geo_nav_check_box_rotated(
    const GeoNavGrid* grid, const GeoBoxRotated* boxRotated, const GeoNavCond cond) {
  const GeoBox       bounds = geo_box_from_rotated(&boxRotated->box, boxRotated->rotation);
  const GeoNavRegion region = nav_cell_map_box(grid, &bounds);
  for (u32 y = region.min.y; y != region.max.y; ++y) {
    u32 cellIndex = nav_cell_index(grid, (GeoNavCell){.x = region.min.x, .y = y});
    for (u32 x = region.min.x; x != region.max.x; ++x, ++cellIndex) {
      const GeoNavCell cell = {.x = x, .y = y};
      if (!g_geoNavPredCond[cond](grid, null, cellIndex)) {
        continue; // Doesn't meet condition.
      }
      const GeoBox cellBox = nav_cell_box(grid, cell);
      if (!geo_box_rotated_overlap_box(boxRotated, &cellBox)) {
        continue; // Not overlapping.
      }
      return true; // Meets condition and overlaps.
    }
  }
  return false;
}

bool geo_nav_check_sphere(const GeoNavGrid* grid, const GeoSphere* sphere, const GeoNavCond cond) {
  const GeoBox       bounds = geo_box_from_sphere(sphere->point, sphere->radius);
  const GeoNavRegion region = nav_cell_map_box(grid, &bounds);
  for (u32 y = region.min.y; y != region.max.y; ++y) {
    u32 cellIndex = nav_cell_index(grid, (GeoNavCell){.x = region.min.x, .y = y});
    for (u32 x = region.min.x; x != region.max.x; ++x, ++cellIndex) {
      const GeoNavCell cell = {.x = x, .y = y};
      if (!g_geoNavPredCond[cond](grid, null, cellIndex)) {
        continue; // Doesn't meet condition.
      }
      const GeoBox cellBox = nav_cell_box(grid, cell);
      if (!geo_sphere_overlap_box(sphere, &cellBox)) {
        continue; // Not overlapping.
      }
      return true; // Meets condition and overlaps.
    }
  }
  return false;
}

bool geo_nav_check_channel(
    const GeoNavGrid* g, const GeoVector from, const GeoVector to, const GeoNavCond cond) {

  ++nav_worker_state(g)->stats[GeoNavStat_ChannelQueries]; // Track the amount of channel checks.

  const GeoVector localFrom = geo_vector_mul(geo_vector_sub(from, g->cellOffset), g->cellDensity);
  const GeoVector localTo   = geo_vector_mul(geo_vector_sub(to, g->cellOffset), g->cellDensity);
  const NavLine2D localLine = nav_line_create(localFrom, localTo);

  const f32          chanRadius = geo_nav_channel_radius_frac;
  const GeoBox       chanBounds = geo_box_from_capsule(localFrom, localTo, chanRadius);
  const GeoNavRegion chanRegion = nav_cell_map_box_local(g, &chanBounds);

  /**
   * Crude (conservative) estimation of a Minkowski-sum.
   * NOTE: Ignores the fact that the summed shape should have rounded corners, meaning we detect
   * intersections too early at the corners.
   */
  const f32 localExtent = 1.0f + chanRadius;

  for (u32 y = chanRegion.min.y; y != chanRegion.max.y; ++y) {
    u32 cellIndex = nav_cell_index(g, (GeoNavCell){.x = chanRegion.min.x, .y = y});
    for (u32 x = chanRegion.min.x; x != chanRegion.max.x; ++x, ++cellIndex) {
      const GeoNavCell cell = {.x = x, .y = y};
      if (!g_geoNavPredCond[cond](g, null, cellIndex)) {
        continue; // Doesn't meet condition.
      }
      const NavRect2D cellRect = {.pos = {(f32)cell.x, (f32)cell.y}, .extent = localExtent};
      if (!nav_line_intersect_rect(&localLine, &cellRect)) {
        continue; // Not overlapping.
      }
      return true; // Meets condition and overlaps.
    }
  }
  return false;
}

GeoNavCell geo_nav_closest(const GeoNavGrid* grid, const GeoNavCell cell, const GeoNavCond cond) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);

  GeoNavWorkerState*  s = nav_worker_state(grid);
  GeoNavCell          res[1];
  GeoNavCellContainer container = {.cells = res, .capacity = array_elems(res)};
  if (nav_find(grid, s, &cond, cell, nav_pred_condition, container)) {
    return res[0];
  }
  return cell; // No matching cell found.
}

u32 geo_nav_closest_n(
    const GeoNavGrid*         grid,
    const GeoNavCell          cell,
    const GeoNavCond          cond,
    const GeoNavCellContainer out) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);

  GeoNavWorkerState* s = nav_worker_state(grid);
  return nav_find(grid, s, &cond, cell, nav_pred_condition, out);
}

GeoNavCell
geo_nav_closest_reachable(const GeoNavGrid* grid, const GeoNavCell from, const GeoNavCell to) {
  diag_assert(from.x < grid->cellCountAxis && from.y < grid->cellCountAxis);
  diag_assert(to.x < grid->cellCountAxis && to.y < grid->cellCountAxis);

  GeoNavWorkerState*  s             = nav_worker_state(grid);
  const u32           fromCellIndex = nav_cell_index(grid, from);
  const GeoNavIsland  fromIsland    = nav_island(grid, fromCellIndex);
  GeoNavCell          res[1];
  GeoNavCellContainer container = {.cells = res, .capacity = array_elems(res)};
  if (nav_find(grid, s, &fromIsland, to, nav_pred_reachable, container)) {
    return res[0];
  }
  return from; // No reachable cell found.
}

u32 geo_nav_path(
    const GeoNavGrid*         grid,
    const GeoNavCell          from,
    const GeoNavCell          to,
    const GeoNavCellContainer out) {
  diag_assert(from.x < grid->cellCountAxis && from.y < grid->cellCountAxis);
  diag_assert(to.x < grid->cellCountAxis && to.y < grid->cellCountAxis);

  const u32 fromCellIndex = nav_cell_index(grid, from);
  const u32 toCellIndex   = nav_cell_index(grid, to);

  if (nav_pred_blocked(grid, null, fromCellIndex)) {
    return 0; // From cell is blocked; no path possible.
  }
  if (nav_island(grid, fromCellIndex) != nav_island(grid, toCellIndex)) {
    return 0; // Cells are on different islands; no path possible.
  }

  GeoNavWorkerState* s = nav_worker_state(grid);
  if (nav_path(grid, s, from, to)) {
    return nav_path_output(grid, s, from, to, out);
  }
  return 0;
}

static void geo_nav_block_box(
    GeoNavGrid* grid, const GeoNavRegion region, BitSet regionBits, const GeoBox* box) {
  u16 indexInRegion = 0;
  for (u32 y = region.min.y; y != region.max.y; ++y) {
    u32 cellIndex = nav_cell_index(grid, (GeoNavCell){.x = region.min.x, .y = y});
    for (u16 x = region.min.x; x != region.max.x; ++x, ++cellIndex) {
      const f32 cellY = grid->cellY[cellIndex];
      if (box->max.y > cellY && box->min.y < (cellY + grid->cellHeight)) {
        if (!nav_bit_test(regionBits, indexInRegion)) {
          nav_cell_block(grid, cellIndex);
          nav_bit_set(regionBits, indexInRegion);
        }
      }
      ++indexInRegion;
    }
  }
}

static void geo_nav_block_box_rotated(
    GeoNavGrid* grid, const GeoNavRegion region, BitSet regionBits, const GeoBoxRotated* box) {
  u16 indexInRegion = 0;
  for (u32 y = region.min.y; y != region.max.y; ++y) {
    u32 cellIndex = nav_cell_index(grid, (GeoNavCell){.x = region.min.x, .y = y});
    for (u32 x = region.min.x; x != region.max.x; ++x, ++cellIndex) {
      const GeoNavCell cell    = {.x = x, .y = y};
      const GeoBox     cellBox = nav_cell_box(grid, cell);
      if (geo_box_rotated_overlap_box(box, &cellBox)) {
        if (!nav_bit_test(regionBits, indexInRegion)) {
          nav_cell_block(grid, cellIndex);
          nav_bit_set(regionBits, indexInRegion);
        }
      }
      ++indexInRegion;
    }
  }
}

static void geo_nav_block_sphere(
    GeoNavGrid* grid, const GeoNavRegion region, BitSet regionBits, const GeoSphere* sphere) {
  u16 indexInRegion = 0;
  for (u32 y = region.min.y; y != region.max.y; ++y) {
    u32 cellIndex = nav_cell_index(grid, (GeoNavCell){.x = region.min.x, .y = y});
    for (u32 x = region.min.x; x != region.max.x; ++x, ++cellIndex) {
      const GeoNavCell cell    = {.x = x, .y = y};
      const GeoBox     cellBox = nav_cell_box(grid, cell);
      if (geo_sphere_overlap_box(sphere, &cellBox)) {
        if (!nav_bit_test(regionBits, indexInRegion)) {
          nav_cell_block(grid, cellIndex);
          nav_bit_set(regionBits, indexInRegion);
        }
      }
      ++indexInRegion;
    }
  }
}

static void geo_nav_block_shape(
    GeoNavGrid* grid, const GeoNavRegion region, BitSet regionBits, const GeoBlockerShape* shape) {
  switch (shape->type) {
  case GeoBlockerType_Box:
    geo_nav_block_box(grid, region, regionBits, &shape->box);
    return;
  case GeoBlockerType_BoxRotated:
    geo_nav_block_box_rotated(grid, region, regionBits, &shape->boxRotated);
    return;
  case GeoBlockerType_Sphere:
    geo_nav_block_sphere(grid, region, regionBits, &shape->sphere);
    return;
  }
  UNREACHABLE
}

static GeoBox geo_nav_block_bounds_shape(const GeoBlockerShape* shape) {
  switch (shape->type) {
  case GeoBlockerType_Box:
    return shape->box;
  case GeoBlockerType_BoxRotated:
    return geo_box_from_rotated(&shape->boxRotated.box, shape->boxRotated.rotation);
  case GeoBlockerType_Sphere:
    return geo_box_from_sphere(shape->sphere.point, shape->sphere.radius);
  }
  UNREACHABLE
}

NO_INLINE_HINT static void geo_nav_report_blocker_too_big(const GeoNavRegion blockerRegion) {
  log_e(
      "Navigation blocker cell limit reached",
      log_param("cells", fmt_int(nav_region_size(blockerRegion))),
      log_param("limit", fmt_int(geo_nav_blocker_max_cells)));
}

GeoNavBlockerId geo_nav_blocker_add(
    GeoNavGrid* grid, const u64 userId, const GeoBlockerShape* shapes, const u32 shapeCnt) {
  if (UNLIKELY(!shapeCnt)) {
    return geo_blocker_invalid;
  }

  GeoBox bounds = geo_nav_block_bounds_shape(&shapes[0]);
  for (u32 i = 1; i != shapeCnt; ++i) {
    const GeoBox shapeBounds = geo_nav_block_bounds_shape(&shapes[i]);
    bounds                   = geo_box_encapsulate_box(&bounds, &shapeBounds);
  }
  const GeoNavRegion region = nav_cell_map_box(grid, &bounds);
  if (UNLIKELY(nav_region_size(region) > geo_nav_blocker_max_cells)) {
    geo_nav_report_blocker_too_big(region);
    return geo_blocker_invalid; // TODO: Switch to a heap allocation for big blockers?
  }

  const GeoNavBlockerId blockerId = nav_blocker_acquire(grid);
  if (UNLIKELY(sentinel_check(blockerId))) {
    return geo_blocker_invalid;
  }
  GeoNavBlocker* blocker = &grid->blockers[blockerId];
  blocker->userId        = userId;
  blocker->region        = region;

  const BitSet blockedInRegion = bitset_from_array(blocker->blockedInRegion);
  bitset_clear_all(blockedInRegion);

  for (u32 i = 0; i != shapeCnt; ++i) {
    geo_nav_block_shape(grid, region, blockedInRegion, &shapes[i]);
  }

  ++grid->stats[GeoNavStat_BlockerAddCount]; // Track amount of blocker additions.
  return blockerId;
}

bool geo_nav_blocker_remove(GeoNavGrid* grid, const GeoNavBlockerId blockerId) {
  if (sentinel_check(blockerId)) {
    return false; // Blocker was never actually added; no need to remove it.
  }
  return nav_blocker_release(grid, blockerId);
}

bool geo_nav_blocker_remove_pred(
    GeoNavGrid* grid, const GeoNavBlockerPredicate predicate, void* ctx) {

  bool anyBecameUnblocked = false;
  for (GeoNavBlockerId blockerId = 0; blockerId != geo_nav_blockers_max; ++blockerId) {
    if (nav_bit_test(grid->blockerFreeSet, blockerId)) {
      continue; // Blocker is unused.
    }
    if (predicate(ctx, grid->blockers[blockerId].userId)) {
      anyBecameUnblocked |= nav_blocker_release(grid, blockerId);
    }
  }
  return anyBecameUnblocked;
}

bool geo_nav_blocker_remove_all(GeoNavGrid* grid) { return nav_blocker_release_all(grid); }

bool geo_nav_blocker_reachable(
    const GeoNavGrid* grid, const GeoNavBlockerId blockerId, const GeoNavCell from) {
  diag_assert(from.x < grid->cellCountAxis && from.y < grid->cellCountAxis);

  if (sentinel_check(blockerId)) {
    return false; // Blocker was never actually added; not reachable.
  }
  const u32          fromCellIndex = nav_cell_index(grid, from);
  const GeoNavIsland island        = nav_island(grid, fromCellIndex);
  if (island == geo_nav_island_blocked) {
    return false; // From cell is blocked; not reachable.
  }

  ++nav_worker_state(grid)->stats[GeoNavStat_BlockerReachableQueries]; // Track query count.

  return nav_blocker_neighbors_island(grid, blockerId, island);
}

GeoNavCell geo_nav_blocker_closest(
    const GeoNavGrid* grid, const GeoNavBlockerId blockerId, const GeoNavCell from) {
  diag_assert(from.x < grid->cellCountAxis && from.y < grid->cellCountAxis);

  if (sentinel_check(blockerId)) {
    return from; // Blocker was never actually added; not reachable.
  }
  const u32 fromCellIndex = nav_cell_index(grid, from);
  if (nav_island(grid, fromCellIndex) == geo_nav_island_blocked) {
    return from; // Origin position is blocked.
  }

  ++nav_worker_state(grid)->stats[GeoNavStat_BlockerClosestQueries]; // Track query count.

  return nav_blocker_closest_reachable(grid, blockerId, from);
}

bool geo_nav_island_update(GeoNavGrid* grid, const bool refresh) {
  GeoNavIslandUpdater* u = &grid->islandUpdater;
  if (refresh) {
    u->flags |= GeoNavIslandUpdater_Dirty;
  }
  const bool isDirty = (u->flags & GeoNavIslandUpdater_Dirty) != 0;
  if (isDirty && (u->flags & GeoNavIslandUpdater_Active) == 0) {
    nav_island_update_start(grid);
  }
  if (u->flags & GeoNavIslandUpdater_Active) {
    if (nav_island_update_tick(grid) == NavIslandUpdate_Done) {
      nav_island_update_stop(grid);
    }
  }
  return (u->flags & GeoNavIslandUpdater_Busy) != 0;
}

void geo_nav_occupant_add(
    GeoNavGrid*               grid,
    const u64                 userId,
    const GeoVector           pos,
    const f32                 radius,
    const f32                 weight,
    const GeoNavOccupantFlags flags) {
  diag_assert(radius > f32_epsilon); // TODO: Decide if 0 radius is valid.
  diag_assert(weight > f32_epsilon);
  if (UNLIKELY(grid->occupantCount == geo_nav_occupants_max)) {
    log_e("Navigation occupant limit reached", log_param("limit", fmt_int(geo_nav_occupants_max)));
    return;
  }
  const GeoNavMapResult mapRes = nav_cell_map(grid, pos);
  if (mapRes.flags & (GeoNavMap_ClampedX | GeoNavMap_ClampedY)) {
    return; // Occupant outside of the grid.
  }
  if (!(flags & GeoNavOccupantFlags_Moving)) {
    nav_bit_set(grid->cellOccupiedStationarySet, nav_cell_index(grid, mapRes.cell));
  }
  const u16 occupantIndex        = grid->occupantCount++;
  grid->occupants[occupantIndex] = (GeoNavOccupant){
      .userId = userId,
      .radius = radius,
      .weight = weight,
      .flags  = flags,
      .pos    = {pos.x, pos.z},
  };
  const u32 cellIndex = nav_cell_index(grid, mapRes.cell);
  nav_cell_add_occupant(grid, cellIndex, occupantIndex);
}

void geo_nav_occupant_remove_all(GeoNavGrid* grid) {
  mem_set(nav_occupancy_mem(grid), 255);
  mem_set(grid->cellOccupiedStationarySet, 0);
  grid->occupantCount = 0;
}

GeoVector geo_nav_separate_from_blockers(const GeoNavGrid* grid, const GeoVector pos) {
  const GeoNavMapResult mapRes = nav_cell_map(grid, pos);
  if (mapRes.flags & (GeoNavMap_ClampedX | GeoNavMap_ClampedY)) {
    return geo_vector(0); // Position outside of the grid.
  }
  const u32 cellIndex = nav_cell_index(grid, mapRes.cell);
  if (nav_pred_blocked(grid, null, cellIndex)) {
    // Position is currently in a blocked cell; push it into the closest unblocked cell.
    const GeoNavCell closestUnblocked = geo_nav_closest(grid, mapRes.cell, GeoNavCond_Unblocked);
    return geo_vector_sub(nav_cell_pos(grid, closestUnblocked), pos);
  }
  // Compute the local region to use, retrieves 3x3 cells around the position.
  const GeoNavRegion region = nav_cell_grow(grid, mapRes.cell, 1);
  diag_assert(nav_region_size(region) <= (3 * 3));

  return nav_separate_from_blockers(grid, region, pos);
}

GeoVector geo_nav_separate_from_occupants(
    const GeoNavGrid* grid,
    const u64         userId,
    const GeoVector   pos,
    const f32         radius,
    const f32         weight) {
  diag_assert(radius > f32_epsilon); // TODO: Decide if 0 radius is valid.
  diag_assert(weight > f32_epsilon);
  const GeoNavMapResult mapRes = nav_cell_map(grid, pos);
  if (mapRes.flags & (GeoNavMap_ClampedX | GeoNavMap_ClampedY)) {
    return geo_vector(0); // Position outside of the grid.
  }
  const u32 cellIndex = nav_cell_index(grid, mapRes.cell);
  if (nav_pred_blocked(grid, null, cellIndex)) {
    return geo_vector(0); // Position on the blocked cell.
  }
  // Compute the local region to use, retrieves 3x3 cells around the position.
  const GeoNavRegion region = nav_cell_grow(grid, mapRes.cell, 1);
  diag_assert(nav_region_size(region) <= (3 * 3));

  return nav_separate_from_occupied(grid, region, userId, pos, radius, weight);
}

void geo_nav_stats_reset(GeoNavGrid* grid) {
  mem_set(array_mem(grid->stats), 0);
  for (u32 i = 0; i != geo_nav_workers_max; ++i) {
    GeoNavWorkerState* state = grid->workerStates[i];
    if (state) {
      mem_set(array_mem(state->stats), 0);
    }
  }
}

u32* geo_nav_stats(GeoNavGrid* grid) {
  u32 dataSizeGrid = sizeof(GeoNavGrid);
  dataSizeGrid += ((u32)sizeof(f32) * grid->cellCountTotal);          // grid.cellY
  dataSizeGrid += ((u32)sizeof(u8) * grid->cellCountTotal);           // grid.cellBlockerCount
  dataSizeGrid += ((u32)nav_occupancy_mem(grid).size);                // grid.cellOccupancy
  dataSizeGrid += ((u32)sizeof(GeoNavIsland) * grid->cellCountTotal); // grid.cellIslands
  dataSizeGrid += (sizeof(GeoNavBlocker) * geo_nav_blockers_max);     // grid.blockers
  dataSizeGrid += bits_to_bytes(geo_nav_blockers_max);                // grid.blockerFreeSet
  dataSizeGrid += (sizeof(GeoNavOccupant) * geo_nav_occupants_max);   // grid.occupants
  dataSizeGrid += (bits_to_bytes(grid->cellCountTotal) + 1); // grid.cellOccupiedStationarySet
  dataSizeGrid += (bits_to_bytes(grid->cellCountTotal) + 1); // grid.islandUpdater.markedCells

  u32 dataSizePerWorker = sizeof(GeoNavWorkerState);
  dataSizePerWorker += (bits_to_bytes(grid->cellCountTotal) + 1);   // state.markedCells
  dataSizePerWorker += (sizeof(u16) * grid->cellCountTotal);        // state.costs
  dataSizePerWorker += (sizeof(GeoNavCell) * grid->cellCountTotal); // state.cameFrom

  grid->stats[GeoNavStat_CellCountTotal] = grid->cellCountTotal;
  grid->stats[GeoNavStat_CellCountAxis]  = grid->cellCountAxis;
  grid->stats[GeoNavStat_BlockerCount]   = nav_blocker_count(grid);
  grid->stats[GeoNavStat_IslandCount]    = grid->islandCount;
  grid->stats[GeoNavStat_OccupantCount]  = grid->occupantCount;
  grid->stats[GeoNavStat_GridDataSize]   = dataSizeGrid;
  grid->stats[GeoNavStat_WorkerDataSize] = 0;

  // Gather the stats from the workers.
  for (u32 i = 0; i != geo_nav_workers_max; ++i) {
    GeoNavWorkerState* state = grid->workerStates[i];
    if (state) {
      for (u32 stat = 0; stat != array_elems(grid->stats); ++stat) {
        grid->stats[stat] += state->stats[stat];
        state->stats[stat] = 0;
      }
      grid->stats[GeoNavStat_WorkerDataSize] += dataSizePerWorker;
    }
  }

  return grid->stats;
}
