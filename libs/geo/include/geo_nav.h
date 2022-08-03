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
 */
typedef struct {
  GeoNavCell min, max;
} GeoNavRegion;

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

/**
 * Retrieve information about the given cell.
 */
GeoVector geo_nav_position(const GeoNavGrid*, GeoNavCell);
GeoBox    geo_nav_box(const GeoNavGrid*, GeoNavCell);
bool      geo_nav_blocked(const GeoNavGrid*, GeoNavCell);

/**
 * Lookup cells.
 */
GeoNavCell geo_nav_cell_from_position(const GeoNavGrid*, GeoVector);

/**
 * Remove all blockers from the grid.
 */
void geo_nav_blocker_clear_all(GeoNavGrid*);

/**
 * Mark the given region on the grid as blocked.
 */
void geo_nav_blocker_add_box(GeoNavGrid*, const GeoBox*);
void geo_nav_blocker_add_box_rotated(GeoNavGrid*, const GeoBoxRotated*);
