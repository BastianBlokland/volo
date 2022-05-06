#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_box.h"
#include "geo_color.h"

ecs_comp_extern(DebugShapeComp);

/**
 * Add a new debug-shape component to the given entity.
 */
DebugShapeComp* debug_shape_create(EcsWorld*, EcsEntityId entity);

/**
 * Draw primitives.
 */
void debug_shape_box_fill(DebugShapeComp*, GeoVector pos, GeoQuat, GeoVector size, GeoColor);
void debug_shape_box_wire(DebugShapeComp*, GeoVector pos, GeoQuat, GeoVector size, GeoColor);
void debug_shape_sphere_fill(DebugShapeComp*, GeoVector pos, f32 radius, GeoColor);
void debug_shape_sphere_wire(DebugShapeComp*, GeoVector pos, f32 radius, GeoColor);
