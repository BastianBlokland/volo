#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_box.h"
#include "geo_color.h"

ecs_comp_extern(DebugShapeCanvasComp);

/**
 * Add a new debug-shape canvas component to the given entity.
 */
DebugShapeCanvasComp* debug_shape_canvas_create(EcsWorld*, EcsEntityId entity);

/**
 * Draw primitives.
 */
void debug_shape_box_fill(DebugShapeCanvasComp*, GeoVector pos, GeoQuat, GeoVector size, GeoColor);
void debug_shape_box_wire(DebugShapeCanvasComp*, GeoVector pos, GeoQuat, GeoVector size, GeoColor);
void debug_shape_sphere_fill(DebugShapeCanvasComp*, GeoVector pos, f32 radius, GeoColor);
void debug_shape_sphere_wire(DebugShapeCanvasComp*, GeoVector pos, f32 radius, GeoColor);
