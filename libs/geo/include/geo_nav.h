#pragma once
#include "geo.h"
#include "geo_box_rotated.h"
#include "geo_sphere.h"

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
 * Cell container.
 */
typedef struct {
  GeoNavCell* cells;
  u32         capacity;
} GeoNavCellContainer;

/**
 * A NavIsland is a reachable area in the grid.
 */
typedef u8 GeoNavIsland;

/**
 * Create a new GeoNavGrid instance.
 * Destroy using 'geo_nav_destroy()'.
 */
GeoNavGrid* geo_nav_grid_create(Allocator*, f32 size, f32 cellSize, f32 height, f32 blockHeight);

/**
 * Destroy a GeoNavGrid instance.
 */
void geo_nav_grid_destroy(GeoNavGrid*);

/**
 * Get the region covering the entire navigation grid.
 */
GeoNavRegion geo_nav_bounds(const GeoNavGrid*);
f32          geo_nav_size(const GeoNavGrid*);
f32          geo_nav_cell_size(const GeoNavGrid*);
f32          geo_nav_channel_radius(const GeoNavGrid*);

/**
 * Update cell y coordinates.
 */
void geo_nav_y_update(GeoNavGrid*, GeoNavCell, f32 y);
void geo_nav_y_clear(GeoNavGrid*);

typedef enum {
  GeoNavCond_Blocked,
  GeoNavCond_Unblocked,
  GeoNavCond_Occupied,
  GeoNavCond_OccupiedStationary,
  GeoNavCond_OccupiedMoving,
  GeoNavCond_Free,    // Not blocked and without a stationary occupant.
  GeoNavCond_NonFree, // Blocked or with an stationary occupant.
} GeoNavCond;

/**
 * Lookup cell information.
 */
u16          geo_nav_manhattan_dist(const GeoNavGrid*, GeoNavCell from, GeoNavCell to);
u16          geo_nav_chebyshev_dist(const GeoNavGrid*, GeoNavCell from, GeoNavCell to);
GeoVector    geo_nav_position(const GeoNavGrid*, GeoNavCell);
GeoNavCell   geo_nav_at_position(const GeoNavGrid*, GeoVector);
GeoNavIsland geo_nav_island(const GeoNavGrid*, GeoNavCell);
bool         geo_nav_reachable(const GeoNavGrid*, GeoNavCell from, GeoNavCell to);

bool geo_nav_check(const GeoNavGrid*, GeoNavCell, GeoNavCond);
bool geo_nav_check_box_rotated(const GeoNavGrid*, const GeoBoxRotated*, GeoNavCond);
bool geo_nav_check_sphere(const GeoNavGrid*, const GeoSphere*, GeoNavCond);
bool geo_nav_check_channel(const GeoNavGrid*, GeoVector from, GeoVector to, GeoNavCond);

GeoNavCell geo_nav_closest(const GeoNavGrid*, GeoNavCell, GeoNavCond);
u32        geo_nav_closest_n(const GeoNavGrid*, GeoNavCell, GeoNavCond, GeoNavCellContainer);
GeoNavCell geo_nav_closest_reachable(const GeoNavGrid*, GeoNavCell from, GeoNavCell to);

/**
 * Compute a path between the given two cells.
 * Returns the amount of cells in the path and writes the output cells to the given cell container.
 * NOTE: Returns 0 when no path is possible.
 */
u32 geo_nav_path(const GeoNavGrid*, GeoNavCell from, GeoNavCell to, GeoNavCellContainer);

/**
 * Register grid blockers.
 */
typedef u16 GeoNavBlockerId;
typedef bool (*GeoNavBlockerPredicate)(const void* context, u64 id);

#define geo_blocker_invalid sentinel_u16

typedef enum {
  GeoBlockerType_Box,
  GeoBlockerType_BoxRotated,
  GeoBlockerType_Sphere,
} GeoBlockerType;

typedef struct {
  GeoBlockerType type;
  union {
    GeoBox        box;
    GeoBoxRotated boxRotated;
    GeoSphere     sphere;
  };
} GeoBlockerShape;

GeoNavBlockerId geo_nav_blocker_add(GeoNavGrid*, u64 userId, const GeoBlockerShape*, u32 shapeCnt);
bool            geo_nav_blocker_remove(GeoNavGrid*, GeoNavBlockerId);
bool            geo_nav_blocker_remove_pred(GeoNavGrid*, GeoNavBlockerPredicate, void* ctx);
bool            geo_nav_blocker_remove_all(GeoNavGrid*);
bool            geo_nav_blocker_reachable(const GeoNavGrid*, GeoNavBlockerId, GeoNavCell from);
GeoNavCell      geo_nav_blocker_closest(const GeoNavGrid*, GeoNavBlockerId, GeoNavCell from);

/**
 * (Re-)compute the islands.
 * NOTE: Island computation will take multiple ticks on larger grids, the return value indicates if
 * the computation is still on-going.
 */
bool geo_nav_island_update(GeoNavGrid*, bool refresh);

/**
 * Register occupants.
 */
typedef enum {
  GeoNavOccupantFlags_Moving = 1 << 0,
} GeoNavOccupantFlags;

void geo_nav_occupant_add(
    GeoNavGrid*, u64 userId, GeoVector pos, f32 radius, f32 weight, GeoNavOccupantFlags);
void geo_nav_occupant_remove_all(GeoNavGrid*);

/**
 * Compute a vector to separate from blockers.
 */
GeoVector geo_nav_separate_from_blockers(const GeoNavGrid*, GeoVector pos);

/**
 * Compute a vector to separate from occupants.
 * NOTE: id can be used to ignore an existing occupant (for example itself).
 */
GeoVector geo_nav_separate_from_occupants(
    const GeoNavGrid*, u64 userId, GeoVector pos, f32 radius, f32 weight);

/**
 * Navigation statistics.
 */

typedef enum {
  GeoNavStat_CellCountTotal,
  GeoNavStat_CellCountAxis,
  GeoNavStat_BlockerCount,
  GeoNavStat_BlockerAddCount,
  GeoNavStat_OccupantCount,
  GeoNavStat_IslandCount,
  GeoNavStat_IslandComputes,
  GeoNavStat_PathCount,
  GeoNavStat_PathOutputCells,
  GeoNavStat_PathItrCells,
  GeoNavStat_PathItrEnqueues,
  GeoNavStat_PathLimiterCount,
  GeoNavStat_FindCount,
  GeoNavStat_FindItrCells,
  GeoNavStat_FindItrEnqueues,
  GeoNavStat_ChannelQueries,
  GeoNavStat_BlockerReachableQueries,
  GeoNavStat_BlockerClosestQueries,
  GeoNavStat_GridDataSize,
  GeoNavStat_WorkerDataSize,

  GeoNavStat_Count,
} GeoNavStat;

void geo_nav_stats_reset(GeoNavGrid*);
u32* geo_nav_stats(GeoNavGrid*);
