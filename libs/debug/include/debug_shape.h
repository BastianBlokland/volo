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
void debug_box_fill(DebugShapeComp*, GeoVector pos, GeoQuat, GeoVector size, GeoColor);
void debug_box_wire(DebugShapeComp*, GeoVector pos, GeoQuat, GeoVector size, GeoColor);
void debug_sphere_fill(DebugShapeComp*, GeoVector pos, f32 radius, GeoColor);
void debug_sphere_wire(DebugShapeComp*, GeoVector pos, f32 radius, GeoColor);
void debug_sphere_overlay(DebugShapeComp*, GeoVector pos, f32 radius, GeoColor);
void debug_cylinder_fill(DebugShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor);
void debug_cylinder_wire(DebugShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor);
void debug_cylinder_overlay(DebugShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor);
