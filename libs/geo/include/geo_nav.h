#pragma once
#include "geo_box_rotated.h"
#include "geo_vector.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Navigation grid.
 */
typedef struct sGeoNavGrid GeoNavGrid;

/**
 * Identifier for a navigation cell.
 */
typedef union {
  struct {
    u16 x, y;
  };
  u32 data;
} GeoNavCell;

/**
 * Rectangular region on the navigation grid.
 * NOTE: Max is exclusive.
 */
typedef struct {
  GeoNavCell min, max;
} GeoNavRegion;

/**
 * Path storage container.
 */
typedef struct {
  GeoNavCell* cells;
  u32         capacity;
} GeoNavPathStorage;

/**
 * Create a new GeoNavGrid instance.
 * Destroy using 'geo_nav_destroy()'.
 */
GeoNavGrid* geo_nav_grid_create(Allocator*, GeoVector center, f32 size, f32 density, f32 height);

/**
 * Destroy a GeoNavGrid instance.
 */
void geo_nav_grid_destroy(GeoNavGrid*);

/**
 * Get the region covering the entire navigation grid.
 */
GeoNavRegion geo_nav_bounds(const GeoNavGrid*);
GeoVector    geo_nav_cell_size(const GeoNavGrid*);

/**
 * Lookup cell information.
 */
GeoVector    geo_nav_position(const GeoNavGrid*, GeoNavCell);
f32          geo_nav_distance(const GeoNavGrid*, GeoNavCell a, GeoNavCell b);
GeoBox       geo_nav_box(const GeoNavGrid*, GeoNavCell);
GeoNavRegion geo_nav_region(const GeoNavGrid*, GeoNavCell, u16 radius);
bool         geo_nav_blocked(const GeoNavGrid*, GeoNavCell);
bool         geo_nav_line_blocked(const GeoNavGrid*, GeoNavCell from, GeoNavCell to);
bool         geo_nav_occupied(const GeoNavGrid*, GeoNavCell);
bool         geo_nav_occupied_moving(const GeoNavGrid*, GeoNavCell);
GeoNavCell   geo_nav_closest_unblocked(const GeoNavGrid*, GeoNavCell);
GeoNavCell   geo_nav_closest_free(const GeoNavGrid*, GeoNavCell);
GeoNavCell   geo_nav_at_position(const GeoNavGrid*, GeoVector);

/**
 * Compute a path between the given two cells.
 * Returns the amount of cells in the path and writes the output cells to the given storage.
 * NOTE: Returns 0 when no path is possible.
 */
u32 geo_nav_path(const GeoNavGrid*, GeoNavCell from, GeoNavCell to, GeoNavPathStorage);

/**
 * Register grid blockers.
 */
typedef u16 GeoNavBlockerId;
typedef bool (*GeoNavBlockerPredicate)(const void* context, u64 id);

GeoNavBlockerId geo_nav_blocker_add_box(GeoNavGrid*, u64 userId, const GeoBox*);
GeoNavBlockerId geo_nav_blocker_add_box_rotated(GeoNavGrid*, u64 userId, const GeoBoxRotated*);
void            geo_nav_blocker_remove(GeoNavGrid*, GeoNavBlockerId);
void            geo_nav_blocker_remove_pred(GeoNavGrid*, GeoNavBlockerPredicate, void* ctx);
void            geo_nav_blocker_remove_all(GeoNavGrid*);

/**
 * Register occupants.
 */
typedef enum {
  GeoNavOccupantFlags_Moving = 1 << 0,
} GeoNavOccupantFlags;

void geo_nav_occupant_add(GeoNavGrid*, u64 userId, GeoVector pos, f32 radius, GeoNavOccupantFlags);
void geo_nav_occupant_remove_all(GeoNavGrid*);

/**
 * Compute a vector to separate the given position from blockers and other occupants.
 * NOTE: id can be used to ignore an existing occupant (for example itself).
 */
GeoVector
geo_nav_separate(const GeoNavGrid*, u64 userId, GeoVector pos, f32 radius, GeoNavOccupantFlags);

/**
 * Navigation statistics.
 */

typedef enum {
  GeoNavStat_CellCountTotal,
  GeoNavStat_CellCountAxis,
  GeoNavStat_BlockerCount,
  GeoNavStat_BlockerAddCount,
  GeoNavStat_OccupantCount,
  GeoNavStat_PathCount,
  GeoNavStat_PathOutputCells,
  GeoNavStat_PathItrCells,
  GeoNavStat_PathItrEnqueues,
  GeoNavStat_FindCount,
  GeoNavStat_FindItrCells,
  GeoNavStat_FindItrEnqueues,
  GeoNavStat_LineQueryCount,
  GeoNavStat_GridDataSize,
  GeoNavStat_WorkerDataSize,

  GeoNavStat_Count,
} GeoNavStat;

void geo_nav_stats_reset(GeoNavGrid*);
u32* geo_nav_stats(GeoNavGrid*);
