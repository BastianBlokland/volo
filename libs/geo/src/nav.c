#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"
#include "geo_nav.h"
#include "jobs_executor.h"
#include "log_logger.h"

#include "intrinsic_internal.h"

#define geo_nav_workers_max 64
#define geo_nav_occupants_max 1024
#define geo_nav_occupants_per_cell 4

ASSERT(geo_nav_occupants_max < u16_max, "Nav occupant has to be indexable by a u16");

typedef bool (*NavCellPredicate)(const GeoNavGrid*, GeoNavCell);

typedef struct {
  u64                 id;
  f32                 radius;
  GeoNavOccupantFlags flags;
  GeoVector           pos;
} GeoNavOccupant;

typedef struct {
  BitSet      markedCells;
  GeoNavCell* fScoreQueue; // Cell queue sorted on the fScore, highest first.
  u32         fScoreQueueCount;
  u16*        gScores;
  u16*        fScores;
  GeoNavCell* cameFrom;

  u32 stats[GeoNavStat_Count];
} GeoNavWorkerState;

struct sGeoNavGrid {
  u32             cellCountAxis, cellCountTotal;
  f32             cellDensity, cellSize;
  f32             cellHeight;
  GeoVector       cellOffset;
  BitSet          blockedCells;  // bit[cellCountTotal]
  u16*            cellOccupancy; // u16[cellCountTotal][geo_nav_occupants_per_cell]
  GeoNavOccupant* occupants;     // GeoNavOccupant[geo_nav_occupants_max]
  u32             occupantCount;

  GeoNavWorkerState* workerStates[geo_nav_workers_max];
  Allocator*         alloc;

  u32 stats[GeoNavStat_Count];
};

static GeoNavWorkerState* nav_worker_state(const GeoNavGrid* grid) {
  diag_assert(g_jobsWorkerId < geo_nav_workers_max);
  if (UNLIKELY(!grid->workerStates[g_jobsWorkerId])) {
    GeoNavWorkerState* state = alloc_alloc_t(grid->alloc, GeoNavWorkerState);

    *state = (GeoNavWorkerState){
        .markedCells = alloc_alloc(grid->alloc, bits_to_bytes(grid->cellCountTotal) + 1, 1),
        .fScoreQueue = alloc_array_t(grid->alloc, GeoNavCell, grid->cellCountTotal),
        .gScores     = alloc_array_t(grid->alloc, u16, grid->cellCountTotal),
        .fScores     = alloc_array_t(grid->alloc, u16, grid->cellCountTotal),
        .cameFrom    = alloc_array_t(grid->alloc, GeoNavCell, grid->cellCountTotal),
    };
    ((GeoNavGrid*)grid)->workerStates[g_jobsWorkerId] = state;
  }
  return grid->workerStates[g_jobsWorkerId];
}

INLINE_HINT static u16 nav_abs_i16(const i16 v) {
  const i16 mask = v >> 15;
  return (v + mask) ^ mask;
}

INLINE_HINT static u16 nav_min_u16(const u16 a, const u16 b) { return a < b ? a : b; }

INLINE_HINT static void nav_swap_u16(u16* a, u16* b) {
  const u16 temp = *a;
  *a             = *b;
  *b             = temp;
}

/**
 * Compute the total amount of cells in the region.
 */
INLINE_HINT static u32 nav_region_size(GeoNavRegion region) {
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

static Mem nav_occupancy_mem(GeoNavGrid* grid) {
  const usize size = sizeof(u16) * grid->cellCountTotal * geo_nav_occupants_per_cell;
  return mem_create(grid->cellOccupancy, size);
}

static u32 nav_cell_neighbors(const GeoNavGrid* grid, const GeoNavCell cell, GeoNavCell out[4]) {
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

/**
 * Get all the occupants in the given cell.
 * Returns the amount of occupants written to the out array.
 */
static u32 nav_cell_occupants(
    const GeoNavGrid*     grid,
    const GeoNavCell      cell,
    const GeoNavOccupant* out[geo_nav_occupants_per_cell]) {
  u32       count = 0;
  const u32 index = nav_cell_index(grid, cell) * geo_nav_occupants_per_cell;
  for (u32 i = index; i != index + geo_nav_occupants_per_cell; ++i) {
    if (!sentinel_check(grid->cellOccupancy[i])) {
      out[count++] = &grid->occupants[grid->cellOccupancy[i]];
    }
  }
  return count;
}

static bool nav_cell_add_occupant(GeoNavGrid* grid, const GeoNavCell cell, const u16 occupantIdx) {
  const u32 index = nav_cell_index(grid, cell) * geo_nav_occupants_per_cell;
  for (u32 i = index; i != index + geo_nav_occupants_per_cell; ++i) {
    if (sentinel_check(grid->cellOccupancy[i])) {
      grid->cellOccupancy[i] = occupantIdx;
      return true;
    }
  }
  return false; // Maximum occupants per cell reached.
}

static GeoVector nav_cell_pos(const GeoNavGrid* grid, const GeoNavCell cell) {
  const GeoVector localPos = {cell.x, 0, cell.y};
  return geo_vector_add(geo_vector_mul(localPos, grid->cellSize), grid->cellOffset);
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
  GeoVector localPos = geo_vector_round_nearest(
      geo_vector_mul(geo_vector_sub(pos, grid->cellOffset), grid->cellDensity));

  GeoNavMapFlags flags = 0;
  if (UNLIKELY(nav_cell_clamp_axis(grid, &localPos.x))) {
    flags |= GeoNavMap_ClampedX;
  }
  if (UNLIKELY(nav_cell_clamp_axis(grid, &localPos.z))) {
    flags |= GeoNavMap_ClampedY;
  }
  return (GeoNavMapResult){
      .cell  = {.x = (u16)localPos.x, .y = (u16)localPos.z},
      .flags = flags,
  };
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

static GeoNavRegion nav_cell_grow(const GeoNavGrid* grid, const GeoNavCell cell, const u16 radius) {
  const u16 minX = cell.x - nav_min_u16(cell.x, radius);
  const u16 minY = cell.y - nav_min_u16(cell.y, radius);
  const u16 maxX = nav_min_u16(cell.x + radius, grid->cellCountAxis - 1) + 1;
  const u16 maxY = nav_min_u16(cell.y + radius, grid->cellCountAxis - 1) + 1;
  return (GeoNavRegion){.min = {.x = minX, .y = minY}, .max = {.x = maxX, .y = maxY}};
}

static f32 nav_cell_dist_sqr(const GeoNavGrid* grid, const GeoNavCell cell, const GeoVector tgt) {
  // NOTE: Could be implemented in 2d on the grid plane.
  const f32       cellRadiusAxis = grid->cellSize * 0.5f + f32_epsilon;
  const GeoVector cellRadius     = geo_vector(cellRadiusAxis, 0, cellRadiusAxis);
  const GeoVector cellPos        = nav_cell_pos(grid, cell);
  const GeoVector deltaMin       = geo_vector_sub(geo_vector_sub(cellPos, cellRadius), tgt);
  const GeoVector deltaMax       = geo_vector_sub(tgt, geo_vector_add(cellPos, cellRadius));
  return geo_vector_mag_sqr(geo_vector_max(geo_vector_max(deltaMin, deltaMax), geo_vector(0)));
}

static u16 nav_path_heuristic(const GeoNavCell from, const GeoNavCell to) {
  /**
   * Basic manhattan distance to estimate the cost between the two cells.
   * Additionally we a multiplier to make the A* search more greedy to reduce the amount of visited
   * cells with the trade-off of less optimal paths.
   */
  enum { ExpectedCostPerCell = 1, Multiplier = 2 };
  const i16 diffX = to.x - (i16)from.x;
  const i16 diffY = to.y - (i16)from.y;
  return (nav_abs_i16(diffX) + nav_abs_i16(diffY)) * ExpectedCostPerCell * Multiplier;
}

/**
 * Insert the given cell into the fScoreQueue, sorted on fScore (highest first).
 * Pre-condition: Cell does not exist in the queue yet.
 */
static void nav_path_enqueue(const GeoNavGrid* grid, GeoNavWorkerState* s, const GeoNavCell c) {
  ++s->stats[GeoNavStat_PathItrEnqueues]; // Track total amount of path cell enqueues.

  /**
   * Binary search to find the first openCell with a lower fScore and insert before it.
   * NOTE: This can probably be implemented more efficiently using some from of a priority queue.
   */
  const u16   fScore = s->fScores[nav_cell_index(grid, c)];
  GeoNavCell* itr    = s->fScoreQueue;
  GeoNavCell* end    = s->fScoreQueue + s->fScoreQueueCount;
  u32         count  = s->fScoreQueueCount;
  while (count) {
    const u32   step   = count / 2;
    GeoNavCell* middle = itr + step;
    if (fScore <= s->fScores[nav_cell_index(grid, *middle)]) {
      itr = middle + 1;
      count -= step + 1;
    } else {
      count = step;
    }
  }
  if (itr == end) {
    // No lower FScore found; insert it at the end.
    s->fScoreQueue[s->fScoreQueueCount++] = c;
  } else {
    // FScore at itr was better; shift the collection 1 towards the end.
    mem_move(mem_from_to(itr + 1, end + 1), mem_from_to(itr, end));
    *itr = c;
    ++s->fScoreQueueCount;
  }
}

static bool
nav_path(const GeoNavGrid* grid, GeoNavWorkerState* s, const GeoNavCell from, const GeoNavCell to) {
  mem_set(s->markedCells, 0);
  mem_set(mem_create(s->fScores, grid->cellCountTotal * sizeof(u16)), 255);
  mem_set(mem_create(s->gScores, grid->cellCountTotal * sizeof(u16)), 255);

  ++s->stats[GeoNavStat_PathCount];       // Track amount of path queries.
  ++s->stats[GeoNavStat_PathItrEnqueues]; // Include the initial enqueue in the tracking.

  s->gScores[nav_cell_index(grid, from)] = 0;
  s->fScores[nav_cell_index(grid, from)] = nav_path_heuristic(from, to);
  s->fScoreQueueCount                    = 1;
  s->fScoreQueue[0]                      = from;

  while (s->fScoreQueueCount) {
    ++s->stats[GeoNavStat_PathItrCells]; // Track total amount of path iterations.

    const GeoNavCell cell      = s->fScoreQueue[--s->fScoreQueueCount];
    const u32        cellIndex = nav_cell_index(grid, cell);
    if (cell.data == to.data) {
      return true; // Destination reached.
    }
    bitset_clear(s->markedCells, cellIndex);

    GeoNavCell neighbors[4];
    const u32  neighborCount = nav_cell_neighbors(grid, cell, neighbors);
    for (u32 i = 0; i != neighborCount; ++i) {
      const GeoNavCell neighbor      = neighbors[i];
      const u32        neighborIndex = nav_cell_index(grid, neighbor);
      if (bitset_test(grid->blockedCells, neighborIndex)) {
        continue; // Ignore blocked cells;
      }
      const u16 tentativeGScore = s->gScores[cellIndex] + 1;
      if (tentativeGScore < s->gScores[neighborIndex]) {
        /**
         * This path to the neighbor is better then the previous, record it and enqueue the neighbor
         * for rechecking.
         */
        s->cameFrom[neighborIndex] = cell;
        s->gScores[neighborIndex]  = tentativeGScore;
        s->fScores[neighborIndex]  = tentativeGScore + nav_path_heuristic(neighbor, to);
        if (!bitset_test(s->markedCells, neighborIndex)) {
          nav_path_enqueue(grid, s, neighbor);
          bitset_set(s->markedCells, neighborIndex);
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
 * Write the computed path to the output storage.
 * NOTE: Only valid if a valid path has been found between the cells using 'nav_path'.
 */
static u32 nav_path_output(
    const GeoNavGrid*       grid,
    GeoNavWorkerState*      s,
    const GeoNavCell        from,
    const GeoNavCell        to,
    const GeoNavPathStorage out) {
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

typedef enum {
  NavFindResult_NotFound,
  NavFindResult_Found,
  NavFindResult_SearchIncomplete, // If there is a result its too far from the starting point.
} NavFindResult;

/**
 * Breadth-first search for a cell matching the given predicate.
 */
static bool nav_find(
    const GeoNavGrid*  grid,
    GeoNavWorkerState* s,
    const GeoNavCell   from,
    NavCellPredicate   predicate,
    GeoNavCell*        outResult) {

  ++s->stats[GeoNavStat_FindCount];       // Track amount of find queries.
  ++s->stats[GeoNavStat_FindItrEnqueues]; // Include the initial enqueue in the tracking.

  GeoNavCell queue[512] = {from};
  u32        queueStart = 0;
  u32        queueEnd   = 1;

  mem_set(s->markedCells, 0);
  bitset_set(s->markedCells, nav_cell_index(grid, from));

  while (queueStart != queueEnd) {
    ++s->stats[GeoNavStat_FindItrCells]; // Track total amount of find iterations.

    const GeoNavCell cell = queue[queueStart++];
    if (predicate(grid, cell)) {
      *outResult = cell;
      return NavFindResult_Found;
    }

    GeoNavCell neighbors[4];
    const u32  neighborCount = nav_cell_neighbors(grid, cell, neighbors);
    for (u32 i = 0; i != neighborCount; ++i) {
      const GeoNavCell neighbor      = neighbors[i];
      const u32        neighborIndex = nav_cell_index(grid, neighbor);
      if (bitset_test(s->markedCells, neighborIndex)) {
        continue;
      }
      if (queueEnd == array_elems(queue)) {
        return NavFindResult_SearchIncomplete;
      }
      ++s->stats[GeoNavStat_FindItrEnqueues]; // Track total amount of find cell enqueues.
      queue[queueEnd++] = neighbor;
      bitset_set(s->markedCells, neighborIndex);
    }
  }
  return NavFindResult_NotFound;
}

/**
 * Check if any cell in a rasterized line 'from' 'to' matches the given predicate.
 */
static bool nav_any_in_line(
    const GeoNavGrid*  grid,
    GeoNavWorkerState* s,
    GeoNavCell         a,
    GeoNavCell         b,
    NavCellPredicate   predicate) {
  ++s->stats[GeoNavStat_LineQueryCount]; // Track the amount of line queries.

  /**
   * Modified verion of Xiaolin Wu's line algorithm.
   */
  const bool steep = nav_abs_i16(b.y - (i16)a.y) > nav_abs_i16(b.x - (i16)a.x);
  if (steep) {
    nav_swap_u16(&a.x, &a.y);
    nav_swap_u16(&b.x, &b.y);
  }
  if (a.x > b.x) {
    nav_swap_u16(&a.x, &b.x);
    nav_swap_u16(&a.y, &b.y);
  }
  const f32 gradient = (b.x - a.x) ? ((b.y - (f32)a.y) / (b.x - (f32)a.x)) : 1.0f;

#define check_cell(_X_, _Y_)                                                                       \
  do {                                                                                             \
    if (predicate(grid, (GeoNavCell){.x = (_X_), .y = (_Y_)})) {                                   \
      return true;                                                                                 \
    }                                                                                              \
  } while (false)

  // A point.
  if (steep) {
    check_cell(a.y, a.x);
    if (a.y != b.y && LIKELY((u16)(a.y + 1) < grid->cellCountAxis)) {
      check_cell(a.y + 1, a.x);
    }
  } else {
    check_cell(a.x, a.y);
    if (a.y != b.y && LIKELY((u16)(a.y + 1) < grid->cellCountAxis)) {
      check_cell(a.x, a.y + 1);
    }
  }

  // Middle points.
  f32 intersectY = a.y + gradient;
  if (steep) {
    for (u16 i = a.x + 1; i < b.x; ++i) {
      check_cell((u16)intersectY, i);
      if (a.y != b.y && LIKELY((u16)(intersectY + 1) < grid->cellCountAxis)) {
        check_cell((u16)intersectY + 1, i);
      }
      intersectY += gradient;
    }
  } else {
    for (u16 i = a.x + 1; i < b.x; ++i) {
      check_cell(i, (u16)intersectY);
      if (a.y != b.y && LIKELY((u16)(intersectY + 1) < grid->cellCountAxis)) {
        check_cell(i, (u16)intersectY + 1);
      }
      intersectY += gradient;
    }
  }

  // B point.
  if (steep) {
    check_cell(b.y, b.x);
    if (a.y != b.y && LIKELY((u16)(b.y + 1) < grid->cellCountAxis)) {
      check_cell(b.y + 1, b.x);
    }
  } else {
    check_cell(b.x, b.y);
    if (a.y != b.y && LIKELY((u16)(b.y + 1) < grid->cellCountAxis)) {
      check_cell(b.x, b.y + 1);
    }
  }

#undef check_cell
  return false; // No cell in the line matched the predicate.
}

static bool nav_cell_predicate_blocked(const GeoNavGrid* grid, const GeoNavCell cell) {
  return bitset_test(grid->blockedCells, nav_cell_index(grid, cell));
}

static bool nav_cell_predicate_unblocked(const GeoNavGrid* grid, const GeoNavCell cell) {
  return !bitset_test(grid->blockedCells, nav_cell_index(grid, cell));
}

static bool nav_cell_predicate_occupied(const GeoNavGrid* grid, const GeoNavCell cell) {
  const GeoNavOccupant* occupants[geo_nav_occupants_per_cell];
  const u32             occupantCount = nav_cell_occupants(grid, cell, occupants);
  return occupantCount != 0;
}

static bool nav_cell_predicate_occupied_moving(const GeoNavGrid* grid, const GeoNavCell cell) {
  const GeoNavOccupant* occupants[geo_nav_occupants_per_cell];
  const u32             occupantCount = nav_cell_occupants(grid, cell, occupants);
  for (u32 i = 0; i != occupantCount; ++i) {
    if (occupants[i]->flags & GeoNavOccupantFlags_Moving) {
      return true;
    }
  }
  return false;
}

/**
 * Get all the occupants in the given region.
 * Returns the amount of occupants written to the out array.
 * NOTE: Array size should be at least 'nav_region_size(region) * geo_nav_occupants_per_cell'.
 */
static u32 nav_region_occupants(
    const GeoNavGrid* grid, const GeoNavRegion region, const GeoNavOccupant** out) {
  u32 count = 0;
  for (u32 y = region.min.y; y != region.max.y; ++y) {
    for (u32 x = region.min.x; x != region.max.x; ++x) {
      const GeoNavCell cell          = {.x = x, .y = y};
      const u32        occupantCount = nav_cell_occupants(grid, cell, out);
      count += occupantCount;
      out += occupantCount;
    }
  }
  return count;
}

INLINE_HINT static f32 nav_separate_weight(const GeoNavOccupantFlags flags) {
  // TODO: Occupant weight should be configurable.
  return (flags & GeoNavOccupantFlags_Moving) ? 4.0f : 1.0f;
}

/**
 * Compute a vector to move an occupant to be at least radius away from any blockers in the region.
 * NOTE: Behaviour is undefined if the position is fully inside a blocked cell.
 */
static GeoVector nav_separate_from_blockers(
    const GeoNavGrid*         grid,
    const GeoNavRegion        reg,
    const GeoVector           pos,
    const f32                 radius,
    const GeoNavOccupantFlags flags) {
  (void)flags;
  /**
   * TODO: Instead of pushing away in the direction of the cell center we should push in the
   * tangent of the axis we're too close in. This improves the scenarios where an object is
   * 'gliding' along multiple blocked cells.
   * NOTE: Could be implemented in 2d on the grid plane.
   */
  GeoVector result = {0};
  for (u32 y = reg.min.y; y != reg.max.y; ++y) {
    for (u32 x = reg.min.x; x != reg.max.x; ++x) {
      const GeoNavCell cell = {.x = x, .y = y};
      // TODO: Optimizable as horizontal neighbors are consecutive in memory.
      if (!nav_cell_predicate_blocked(grid, cell)) {
        continue; // Cell not blocked.
      }
      const f32 distSqr = nav_cell_dist_sqr(grid, cell, pos);
      if (distSqr >= (radius * radius)) {
        continue; // Far enough away.
      }
      const f32       dist    = intrinsic_sqrt_f32(distSqr);
      const GeoVector cellPos = nav_cell_pos(grid, cell);
      const GeoVector sepDir  = geo_vector_norm(geo_vector_sub(pos, cellPos));
      result                  = geo_vector_add(result, geo_vector_mul(sepDir, radius - dist));
    }
  }
  result.y = 0; // Zero out any movement out of the grid's plane.
  return result;
}

/**
 * Compute a vector to move an occupant to be at least radius away any other occupant.
 * NOTE: id can be used to ignore an existing occupant (for example itself).
 * Pre-condition: nav_region_size(region) <= 9.
 */
static GeoVector nav_separate_from_occupied(
    const GeoNavGrid*         grid,
    const GeoNavRegion        region,
    const u64                 id,
    const GeoVector           pos,
    const f32                 radius,
    const GeoNavOccupantFlags flags) {
  const GeoNavOccupant* occupants[(3 * 3) * geo_nav_occupants_per_cell];
  diag_assert((nav_region_size(region) * geo_nav_occupants_per_cell) <= array_elems(occupants));

  const u32 occupantCount = nav_region_occupants(grid, region, occupants);
  const f32 sepWeight     = nav_separate_weight(flags);

  GeoVector result = {0};
  for (u32 i = 0; i != occupantCount; ++i) {
    if (occupants[i]->id == id) {
      continue; // Ignore occupants with the same id.
    }
    const GeoVector toOccupant = geo_vector_sub(occupants[i]->pos, pos);
    const f32       distSqr    = geo_vector_mag_sqr(toOccupant);
    const f32       sepDist    = occupants[i]->radius + radius;
    if (distSqr >= (sepDist * sepDist)) {
      continue; // Far enough away.
    }
    const f32 dist = intrinsic_sqrt_f32(distSqr);
    GeoVector sepDir;
    if (UNLIKELY(dist < f32_epsilon)) {
      // Occupants occupy the exact same position; pick a random direction.
      const GeoQuat rot = geo_quat_angle_axis(geo_up, rng_sample_f32(g_rng) * math_pi_f32 * 2);
      sepDir            = geo_quat_rotate(rot, geo_forward);
    } else {
      sepDir = geo_vector_div(toOccupant, dist);
    }
    const f32 otherWeight = nav_separate_weight(occupants[i]->flags);
    const f32 relWeight   = otherWeight / (sepWeight + otherWeight);

    // NOTE: Times 0.5 because both occupants are expected to move.
    result = geo_vector_add(result, geo_vector_mul(sepDir, (dist - sepDist) * 0.5f * relWeight));
  }
  result.y = 0; // Zero out any movement out of the grid's plane.
  return result;
}

GeoNavGrid* geo_nav_grid_create(
    Allocator* alloc, const GeoVector center, const f32 size, const f32 density, const f32 height) {
  diag_assert(geo_vector_mag_sqr(center) <= (1e4f * 1e4f));
  diag_assert(size > 1e-4f && size < 1e4f);
  diag_assert(density > 1e-4f && density < 1e4f);

  GeoNavGrid* grid           = alloc_alloc_t(alloc, GeoNavGrid);
  const u32   cellCountAxis  = (u32)math_round_nearest_f32(size * density);
  const u32   cellCountTotal = cellCountAxis * cellCountAxis;

  *grid = (GeoNavGrid){
      .cellCountAxis  = cellCountAxis,
      .cellCountTotal = cellCountTotal,
      .cellDensity    = density,
      .cellSize       = 1.0f / density,
      .cellHeight     = height,
      .cellOffset     = geo_vector(center.x + size * -0.5f, center.y, center.z + size * -0.5f),
      .blockedCells   = alloc_alloc(alloc, bits_to_bytes(cellCountTotal) + 1, 1),
      .cellOccupancy  = alloc_array_t(alloc, u16, cellCountTotal * geo_nav_occupants_per_cell),
      .occupants      = alloc_array_t(alloc, GeoNavOccupant, geo_nav_occupants_max),
      .alloc          = alloc,
  };

  bitset_clear_all(grid->blockedCells);
  return grid;
}

void geo_nav_grid_destroy(GeoNavGrid* grid) {
  alloc_free(grid->alloc, grid->blockedCells);
  alloc_free(grid->alloc, nav_occupancy_mem(grid));
  alloc_free_array_t(grid->alloc, grid->occupants, geo_nav_occupants_max);

  for (u32 i = 0; i != geo_nav_workers_max; ++i) {
    GeoNavWorkerState* state = grid->workerStates[i];
    if (state) {
      alloc_free(grid->alloc, state->markedCells);
      alloc_free_array_t(grid->alloc, state->fScoreQueue, grid->cellCountTotal);
      alloc_free_array_t(grid->alloc, state->gScores, grid->cellCountTotal);
      alloc_free_array_t(grid->alloc, state->fScores, grid->cellCountTotal);
      alloc_free_array_t(grid->alloc, state->cameFrom, grid->cellCountTotal);
      alloc_free_t(grid->alloc, state);
    }
  }

  alloc_free_t(grid->alloc, grid);
}

GeoNavRegion geo_nav_bounds(const GeoNavGrid* grid) {
  return (GeoNavRegion){.max = {.x = grid->cellCountAxis, .y = grid->cellCountAxis}};
}

GeoVector geo_nav_cell_size(const GeoNavGrid* grid) {
  return geo_vector(grid->cellSize, grid->cellHeight, grid->cellSize);
}

GeoVector geo_nav_position(const GeoNavGrid* grid, const GeoNavCell cell) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);
  return nav_cell_pos(grid, cell);
}

GeoBox geo_nav_box(const GeoNavGrid* grid, const GeoNavCell cell) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);
  return nav_cell_box(grid, cell);
}

GeoNavRegion geo_nav_region(const GeoNavGrid* grid, const GeoNavCell cell, const u16 radius) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);
  return nav_cell_grow(grid, cell, radius);
}

bool geo_nav_blocked(const GeoNavGrid* grid, const GeoNavCell cell) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);
  return nav_cell_predicate_blocked(grid, cell);
}

bool geo_nav_line_blocked(const GeoNavGrid* grid, const GeoNavCell from, const GeoNavCell to) {
  diag_assert(from.x < grid->cellCountAxis && from.y < grid->cellCountAxis);
  diag_assert(to.x < grid->cellCountAxis && to.y < grid->cellCountAxis);
  /**
   * Check if any cell in a rasterized line between the two points is blocked.
   */
  GeoNavWorkerState* s = nav_worker_state(grid);
  return nav_any_in_line(grid, s, from, to, nav_cell_predicate_blocked);
}

bool geo_nav_occupied(const GeoNavGrid* grid, const GeoNavCell cell) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);
  return nav_cell_predicate_occupied(grid, cell);
}

bool geo_nav_occupied_moving(const GeoNavGrid* grid, const GeoNavCell cell) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);
  return nav_cell_predicate_occupied_moving(grid, cell);
}

GeoNavCell geo_nav_closest_unblocked(const GeoNavGrid* grid, const GeoNavCell cell) {
  diag_assert(cell.x < grid->cellCountAxis && cell.y < grid->cellCountAxis);

  GeoNavWorkerState* s = nav_worker_state(grid);
  GeoNavCell         res;
  if (nav_find(grid, s, cell, nav_cell_predicate_unblocked, &res) == NavFindResult_Found) {
    return res;
  }
  return cell; // No unblocked cell found.
}

GeoNavCell geo_nav_at_position(const GeoNavGrid* grid, const GeoVector pos) {
  return nav_cell_map(grid, pos).cell;
}

u32 geo_nav_path(
    const GeoNavGrid*       grid,
    const GeoNavCell        from,
    const GeoNavCell        to,
    const GeoNavPathStorage out) {
  diag_assert(from.x < grid->cellCountAxis && from.y < grid->cellCountAxis);
  diag_assert(to.x < grid->cellCountAxis && to.y < grid->cellCountAxis);

  if (nav_cell_predicate_blocked(grid, from)) {
    return 0; // From cell is blocked, no path possible.
  }
  if (nav_cell_predicate_blocked(grid, to)) {
    return 0; // To cell is blocked, no path possible.
  }

  GeoNavWorkerState* s = nav_worker_state(grid);
  if (nav_path(grid, s, from, to)) {
    return nav_path_output(grid, s, from, to, out);
  }
  return 0;
}

void geo_nav_blocker_clear_all(GeoNavGrid* grid) { bitset_clear_all(grid->blockedCells); }

void geo_nav_blocker_add_box(GeoNavGrid* grid, const GeoBox* box) {
  if (box->max.y < grid->cellOffset.y || box->min.y > (grid->cellOffset.y + grid->cellHeight)) {
    return; // Outside of the y band of the grid.
  }

  ++grid->stats[GeoNavStat_BlockerBoxCount]; // Track the amount of box blockers.

  const GeoNavRegion region = nav_cell_map_box(grid, box);
  for (u32 y = region.min.y; y != region.max.y; ++y) {
    for (u32 x = region.min.x; x != region.max.x; ++x) {
      const GeoNavCell cell = {.x = x, .y = y};
      // TODO: Optimizable as horizontal neighbors are consecutive in memory.
      bitset_set(grid->blockedCells, nav_cell_index(grid, cell));
    }
  }
}

void geo_nav_blocker_add_box_rotated(GeoNavGrid* grid, const GeoBoxRotated* boxRotated) {
  const GeoBox bounds = geo_box_from_rotated(&boxRotated->box, boxRotated->rotation);

  ++grid->stats[GeoNavStat_BlockerBoxRotatedCount]; // Track the amount of rotated box blockers.

  const GeoNavRegion region = nav_cell_map_box(grid, &bounds);
  for (u32 y = region.min.y; y != region.max.y; ++y) {
    for (u32 x = region.min.x; x != region.max.x; ++x) {
      const GeoNavCell cell    = {.x = x, .y = y};
      const GeoBox     cellBox = nav_cell_box(grid, cell);
      if (geo_box_rotated_overlap_box(boxRotated, &cellBox)) {
        bitset_set(grid->blockedCells, nav_cell_index(grid, cell));
      }
    }
  }
}

void geo_nav_occupant_clear_all(GeoNavGrid* grid) {
  mem_set(nav_occupancy_mem(grid), 255);
  grid->occupantCount = 0;
}

void geo_nav_occupant_add(
    GeoNavGrid*               grid,
    const u64                 id,
    const GeoVector           pos,
    const f32                 radius,
    const GeoNavOccupantFlags flags) {
  diag_assert(radius > f32_epsilon); // TODO: Decide if 0 radius is valid.
  if (UNLIKELY(grid->occupantCount == geo_nav_occupants_max)) {
    log_w("Navigation occupant limit reached", log_param("limit", fmt_int(geo_nav_occupants_max)));
    return;
  }
  const GeoNavMapResult mapRes = nav_cell_map(grid, pos);
  if (mapRes.flags & (GeoNavMap_ClampedX | GeoNavMap_ClampedY)) {
    return; // Occupant outside of the grid.
  }
  const u16 occupantIndex        = grid->occupantCount++;
  grid->occupants[occupantIndex] = (GeoNavOccupant){
      .id     = id,
      .radius = radius,
      .flags  = flags,
      .pos    = pos,
  };
  nav_cell_add_occupant(grid, mapRes.cell, occupantIndex);
}

GeoVector geo_nav_separate(
    const GeoNavGrid*         grid,
    const u64                 id,
    const GeoVector           pos,
    const f32                 radius,
    const GeoNavOccupantFlags flags) {
  diag_assert(radius > f32_epsilon); // TODO: Decide if 0 radius is valid.
  const GeoNavMapResult mapRes = nav_cell_map(grid, pos);
  if (mapRes.flags & (GeoNavMap_ClampedX | GeoNavMap_ClampedY)) {
    return geo_vector(0); // Position outside of the grid.
  }
  // Compute the local region to use, retrieves 3x3 cells around the position.
  const GeoNavRegion region = nav_cell_grow(grid, mapRes.cell, 1);
  GeoVector          result = {0};

  // Separate from blockers.
  if (!nav_cell_predicate_blocked(grid, mapRes.cell)) {
    result = geo_vector_add(result, nav_separate_from_blockers(grid, region, pos, radius, flags));
  }

  // Separate from other occupants.
  result = geo_vector_add(result, nav_separate_from_occupied(grid, region, id, pos, radius, flags));
  return result;
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
  const u32 dataSizeGrid = sizeof(GeoNavGrid) +                              // Structure
                           (bits_to_bytes(grid->cellCountTotal) + 1) +       // grid.blockedCells
                           ((u32)nav_occupancy_mem(grid).size) +             // grid.cellOccupancy
                           (sizeof(GeoNavOccupant) * geo_nav_occupants_max); // grid.occupants

  const u32 dataSizePerWorker = sizeof(GeoNavWorkerState) +                   // Structure
                                (bits_to_bytes(grid->cellCountTotal) + 1) +   // state.markedCells
                                (sizeof(GeoNavCell) * grid->cellCountTotal) + // state.fScoreQueue
                                (sizeof(u16) * grid->cellCountTotal) +        // state.gScores
                                (sizeof(u16) * grid->cellCountTotal) +        // state.fScores
                                (sizeof(GeoNavCell) * grid->cellCountTotal);  // state.cameFrom

  grid->stats[GeoNavStat_CellCountTotal] = grid->cellCountTotal;
  grid->stats[GeoNavStat_CellCountAxis]  = grid->cellCountAxis;
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
