#pragma once
#include "geo_vector.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Navigation environment.
 */
typedef struct sGeoNavEnv GeoNavEnv;

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
 * Create a new GeoNavEnv instance.
 * Destroy using 'geo_nav_destroy()'.
 */
GeoNavEnv* geo_nav_env_create(Allocator*, GeoVector center, f32 size, f32 density);

/**
 * Destroy a GeoNavEnv instance.
 */
void geo_nav_env_destroy(GeoNavEnv*);

/**
 * Retrieve the world position of the given cell.
 */
GeoVector geo_nav_position(const GeoNavEnv*, GeoNavCell);

/**
 * Lookup the cell at the given position.
 */
GeoNavCell geo_nav_cell_from_position(const GeoNavEnv*, GeoVector);
