#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "geo_nav.h"
#include "jobs_executor.h"

#include "intrinsic_internal.h"

#define geo_nav_workers_max 64

typedef struct {
  u8 blockers;
} GeoNavCellData;

typedef struct {
  GeoNavCell* queue;
  u32         queueCount;
  BitSet      openCells;
  u16*        gScores;
  u16*        fScores;
  GeoNavCell* cameFrom;

  u32 statPathCount, statPathOutputCells, statPathItrCells, statPathItrEnqueues;
} GeoNavWorkerState;

struct sGeoNavGrid {
  u32                cellCountAxis, cellCountTotal;
  f32                cellDensity, cellSize;
  f32                cellHeight;
  GeoVector          cellOffset;
  GeoNavCellData*    cells;
  GeoNavWorkerState* workerStates[geo_nav_workers_max];
  Allocator*         alloc;

  u32 statBlockers;
};

static GeoNavWorkerState* nav_worker_state(const GeoNavGrid* grid) {
  diag_assert(g_jobsWorkerId < geo_nav_workers_max);
  if (UNLIKELY(!grid->workerStates[g_jobsWorkerId])) {
    GeoNavWorkerState* state = alloc_alloc_t(grid->alloc, GeoNavWorkerState);

    *state = (GeoNavWorkerState){
        .queue     = alloc_array_t(grid->alloc, GeoNavCell, grid->cellCountTotal),
        .openCells = alloc_alloc(grid->alloc, bits_to_bytes(grid->cellCountTotal) + 1, 1),
        .gScores   = alloc_array_t(grid->alloc, u16, grid->cellCountTotal),
        .fScores   = alloc_array_t(grid->alloc, u16, grid->cellCountTotal),
        .cameFrom  = alloc_array_t(grid->alloc, GeoNavCell, grid->cellCountTotal),
    };
    ((GeoNavGrid*)grid)->workerStates[g_jobsWorkerId] = state;
  }
  return grid->workerStates[g_jobsWorkerId];
}

INLINE_HINT static u16 nav_abs_i16(const i16 v) {
  const i16 mask = v >> 15;
  return (v + mask) ^ mask;
}

INLINE_HINT static u16 nav_cell_index(const GeoNavGrid* grid, const GeoNavCell cell) {
  return cell.y * grid->cellCountAxis + cell.x;
}

static GeoNavCellData* nav_cell_data(GeoNavGrid* grid, const GeoNavCell cell) {
  return &grid->cells[nav_cell_index(grid, cell)];
}

static const GeoNavCellData* nav_cell_data_readonly(const GeoNavGrid* grid, const GeoNavCell cell) {
  return nav_cell_data((GeoNavGrid*)grid, cell);
}

static u32 nav_cell_neighbors(const GeoNavGrid* grid, const GeoNavCell cell, GeoNavCell out[4]) {
  u32 count = 0;
  if ((u16)(cell.x + 1) < grid->cellCountAxis) {
    out[count++] = (GeoNavCell){.x = cell.x + 1, .y = cell.y};
  }
  if (cell.x >= 1) {
    out[count++] = (GeoNavCell){.x = cell.x - 1, .y = cell.y};
  }
  if ((u16)(cell.y + 1) < grid->cellCountAxis) {
    out[count++] = (GeoNavCell){.x = cell.x, .y = cell.y + 1};
  }
  if (cell.y >= 1) {
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

static void nav_cell_block(GeoNavGrid* grid, const GeoNavCell cell) {
  ++nav_cell_data(grid, cell)->blockers;
}

static void nav_clear_cells(GeoNavGrid* grid) {
  mem_set(mem_create(grid->cells, grid->cellCountTotal * sizeof(GeoNavCellData)), 0);
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

static void nav_path_enqueue(const GeoNavGrid* grid, GeoNavWorkerState* s, const GeoNavCell c) {
  ++s->statPathItrEnqueues; // Track total amount of path cell enqueues.

  /**
   * ~Binary search to find the first openCell with a lower fScore and insert before it.
   * NOTE: This can probably be implemented more efficiently using some from of a priority queue.
   */
  const u16   fScore = s->fScores[nav_cell_index(grid, c)];
  GeoNavCell* itr    = s->queue;
  GeoNavCell* end    = s->queue + s->queueCount;
  u32         count  = s->queueCount;
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
    s->queue[s->queueCount++] = c;
  } else {
    // FScore at itr was better; shift the collection 1 towards the end.
    mem_move(mem_from_to(itr + 1, end + 1), mem_from_to(itr, end));
    *itr = c;
    ++s->queueCount;
  }
}

static bool
nav_path(const GeoNavGrid* grid, GeoNavWorkerState* s, const GeoNavCell from, const GeoNavCell to) {
  mem_set(s->openCells, 0);
  mem_set(mem_create(s->fScores, grid->cellCountTotal * sizeof(u16)), 255);
  mem_set(mem_create(s->gScores, grid->cellCountTotal * sizeof(u16)), 255);

  ++s->statPathCount;       // Track amount of path queries.
  ++s->statPathItrEnqueues; // Include the initial enqueue in the tracking.

  s->gScores[nav_cell_index(grid, from)] = 0;
  s->fScores[nav_cell_index(grid, from)] = nav_path_heuristic(from, to);
  s->queueCount                          = 1;
  s->queue[0]                            = from;

  while (s->queueCount) {
    ++s->statPathItrCells; // Track total amount of path iterations.

    const GeoNavCell cell      = s->queue[--s->queueCount];
    const u32        cellIndex = nav_cell_index(grid, cell);
    if (cell.data == to.data) {
      return true; // Destination reached.
    }
    bitset_clear(s->openCells, cellIndex);

    GeoNavCell neighbors[4];
    const u32  neighborCount = nav_cell_neighbors(grid, cell, neighbors);
    for (u32 i = 0; i != neighborCount; ++i) {
      const GeoNavCell neighbor      = neighbors[i];
      const u32        neighborIndex = nav_cell_index(grid, neighbor);
      if (grid->cells[neighborIndex].blockers) {
        continue;
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
        if (!bitset_test(s->openCells, neighborIndex)) {
          nav_path_enqueue(grid, s, neighbor);
          bitset_set(s->openCells, neighborIndex);
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

  ++s->statPathOutputCells; // Track the total amount of output cells
  if (out.capacity > (count - i)) {
    out.cells[count - i] = to;
  }

  for (GeoNavCell itr = to; itr.data != from.data; ++i) {
    ++s->statPathOutputCells; // Track the total amount of output cells.

    itr = s->cameFrom[nav_cell_index(grid, itr)];
    if (out.capacity > (count - 1 - i)) {
      out.cells[count - 1 - i] = itr;
    }
  }
  return math_min(count, out.capacity);
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
      .cells          = alloc_array_t(alloc, GeoNavCellData, cellCountTotal),
      .alloc          = alloc,
  };

  nav_clear_cells(grid);
  return grid;
}

void geo_nav_grid_destroy(GeoNavGrid* grid) {
  alloc_free_array_t(grid->alloc, grid->cells, grid->cellCountTotal);

  for (u32 i = 0; i != geo_nav_workers_max; ++i) {
    GeoNavWorkerState* state = grid->workerStates[i];
    if (state) {
      alloc_free_array_t(grid->alloc, state->queue, grid->cellCountTotal);
      alloc_free(grid->alloc, state->openCells);
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
  return nav_cell_data_readonly(grid, cell)->blockers > 0;
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

  if (nav_cell_data_readonly(grid, from)->blockers) {
    return 0; // From cell is blocked, no path possible.
  }
  if (nav_cell_data_readonly(grid, to)->blockers) {
    return 0; // To cell is blocked, no path possible.
  }

  GeoNavWorkerState* s = nav_worker_state(grid);
  if (nav_path(grid, s, from, to)) {
    return nav_path_output(grid, s, from, to, out);
  }
  return 0;
}

void geo_nav_blocker_clear_all(GeoNavGrid* grid) { nav_clear_cells(grid); }

void geo_nav_blocker_add_box(GeoNavGrid* grid, const GeoBox* box) {
  if (box->max.y < 0 || box->min.y > grid->cellHeight) {
    return; // Outside of the y band of the grid.
  }

  ++grid->statBlockers; // Track the total amount of blockers.

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

  ++grid->statBlockers; // Track the total amount of blockers.

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

void geo_nav_stats_reset(GeoNavGrid* grid) {
  grid->statBlockers = 0;
  for (u32 i = 0; i != geo_nav_workers_max; ++i) {
    GeoNavWorkerState* state = grid->workerStates[i];
    if (state) {
      state->statPathCount       = 0;
      state->statPathOutputCells = 0;
      state->statPathItrCells    = 0;
      state->statPathItrEnqueues = 0;
    }
  }
}

GeoNavStats geo_nav_stats(const GeoNavGrid* grid) {
  const u32 dataSizeGrid      = sizeof(GeoNavCellData) * grid->cellCountTotal; // grid.cells
  const u32 dataSizePerWorker = sizeof(GeoNavCell) * grid->cellCountTotal +    // state.queue
                                (bits_to_bytes(grid->cellCountTotal) + 1) +    // state.openCells
                                sizeof(u16) * grid->cellCountTotal +           // state.gScores
                                sizeof(u16) * grid->cellCountTotal +           // state.fScores
                                sizeof(GeoNavCell) * grid->cellCountTotal;     // state.cameFrom

  GeoNavStats result = {
      .blockerCount = grid->statBlockers,
      .gridDataSize = dataSizeGrid,
  };
  for (u32 i = 0; i != geo_nav_workers_max; ++i) {
    GeoNavWorkerState* state = grid->workerStates[i];
    if (state) {
      result.pathCount += state->statPathCount;
      result.pathOutputCells += state->statPathOutputCells;
      result.pathItrCells += state->statPathItrCells;
      result.pathItrEnqueues += state->statPathItrEnqueues;
      result.workerDataSize += dataSizePerWorker;
    }
  }
  return result;
}
