#pragma once
#include "dev.h"
#include "geo_vector.h"

/**
 * Development grid.
 * NOTE: Initialized on each window entity.
 */
ecs_comp_extern(DevGridComp);

void dev_grid_show(DevGridComp*, f32 height);

/**
 * Snap the given position to the grid.
 * Output is written back to the position pointer.
 */
void dev_grid_snap(const DevGridComp*, GeoVector* position);
void dev_grid_snap_axis(const DevGridComp*, GeoVector* position, u8 axis);

EcsEntityId dev_grid_panel_open(EcsWorld*, EcsEntityId window, DevPanelType);
