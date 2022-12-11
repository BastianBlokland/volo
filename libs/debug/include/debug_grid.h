#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

/**
 * Debug grid.
 * NOTE: Initialized on each window entity.
 */
ecs_comp_extern(DebugGridComp);

void debug_grid_show(DebugGridComp*, f32 height);

/**
 * Snap the given position to the grid.
 * Output is written back to the position pointer..
 */
void debug_grid_snap(const DebugGridComp*, GeoVector* position);
void debug_grid_snap_axis(const DebugGridComp*, GeoVector* position, u8 axis);

EcsEntityId debug_grid_panel_open(EcsWorld*, EcsEntityId window);
