#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "geo_nav.h"
#include "jobs_executor.h"
#include "log_logger.h"

#include "intrinsic_internal.h"

#define geo_nav_workers_max 64
#define geo_nav_occupants_max 1024
#define geo_nav_occupants_per_cell 2

ASSERT(geo_nav_occupants_max < u16_max, "Nav occupant has to be indexable by a u16");

typedef bool (*NavCellPredicate)(const GeoNavGrid*, GeoNavCell);

typedef struct {
  u64       id;
  GeoVector pos;
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

INLINE_HINT static void nav_swap_u16(u16* a, u16* b) {
  const u16 temp = *a;
  *a             = *b;
  *b             = temp;
}

INLINE_HINT static u16 nav_cell_index(const GeoNavGrid* grid, const GeoNavCell cell) {
  return cell.y * grid->cellCountAxis + cell.x;
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
    const GeoNavCell   from,
    const GeoNavCell   to,
    NavCellPredicate   predicate) {
  ++s->stats[GeoNavStat_LineQueryCount]; // Track the amount of line queries.

  /**
   * Modified verion of Xiaolin Wu's line algorithm.
   */
  u16        x0 = from.x, x1 = to.x;
  u16        y0 = from.y, y1 = to.y;
  const bool steep = nav_abs_i16(y1 - (i16)y0) > nav_abs_i16(x1 - (i16)x0);

  if (steep) {
    nav_swap_u16(&x0, &y0);
    nav_swap_u16(&x1, &y1);
  }
  if (x0 > x1) {
    nav_swap_u16(&x0, &x1);
    nav_swap_u16(&y0, &y1);
  }
  const f32 gradient = (x1 - x0) ? ((y1 - (f32)y0) / (x1 - (f32)x0)) : 1.0f;

#define check_cell(_X_, _Y_)                                                                       \
  do {                                                                                             \
    if (predicate(grid, (GeoNavCell){.x = (_X_), .y = (_Y_)})) {                                   \
      return true;                                                                                 \
    }                                                                                              \
  } while (false)

  // From point.
  if (steep) {
    check_cell(y0, x0);
    if (y0 != y1 && LIKELY((u16)(y0 + 1) < grid->cellCountAxis)) {
      check_cell(y0 + 1, x0);
    }
  } else {
    check_cell(x0, y0);
    if (y0 != y1 && LIKELY((u16)(y0 + 1) < grid->cellCountAxis)) {
      check_cell(x0, y0 + 1);
    }
  }

  // Middle points.
  f32 intersectY = y0 + gradient;
  if (steep) {
    for (u16 i = x0 + 1; i < x1; ++i) {
      check_cell((u16)intersectY, i);
      if (y0 != y1 && LIKELY((u16)(intersectY + 1) < grid->cellCountAxis)) {
        check_cell((u16)intersectY + 1, i);
      }
      intersectY += gradient;
    }
  } else {
    for (u16 i = x0 + 1; i < x1; ++i) {
      check_cell(i, (u16)intersectY);
      if (y0 != y1 && LIKELY((u16)(intersectY + 1) < grid->cellCountAxis)) {
        check_cell(i, (u16)intersectY + 1);
      }
      intersectY += gradient;
    }
  }

  // To point.
  if (steep) {
    check_cell(y1, x1);
    if (y0 != y1 && LIKELY((u16)(y1 + 1) < grid->cellCountAxis)) {
      check_cell(y1 + 1, x1);
    }
  } else {
    check_cell(x1, y1);
    if (y0 != y1 && LIKELY((u16)(y1 + 1) < grid->cellCountAxis)) {
      check_cell(x1, y1 + 1);
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
  const u16 index = nav_cell_index(grid, cell) * geo_nav_occupants_per_cell;
  for (u16 i = index; i != index + geo_nav_occupants_per_cell; ++i) {
    if (!sentinel_check(grid->cellOccupancy[i])) {
      return true;
    }
  }
  return false;
}

static bool nav_cell_reg_occupant(GeoNavGrid* grid, const GeoNavCell cell, const u16 occupantIdx) {
  const u16 index = nav_cell_index(grid, cell) * geo_nav_occupants_per_cell;
  for (u16 i = index; i != index + geo_nav_occupants_per_cell; ++i) {
    if (sentinel_check(grid->cellOccupancy[i])) {
      grid->cellOccupancy[i] = occupantIdx;
      return true;
    }
  }
  return false; // Maximum occupants per cell reached.
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

void geo_nav_occupant_add(GeoNavGrid* grid, const GeoVector pos, const u64 id) {
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
      .id  = id,
      .pos = pos,
  };
  nav_cell_reg_occupant(grid, mapRes.cell, occupantIndex);
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
