#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_matrix.h"

typedef enum {
  DebugShape_Fill,
  DebugShape_Wire,
  DebugShape_Overlay,
} DebugShapeMode;

ecs_comp_extern(DebugShapeComp);

/**
 * Add a new debug-shape component to the given entity.
 */
DebugShapeComp* debug_shape_create(EcsWorld*, EcsEntityId entity);

// clang-format off

/**
 * Draw primitives.
 */
void debug_box(DebugShapeComp*, GeoVector pos, GeoQuat, GeoVector size, GeoColor, DebugShapeMode);
void debug_quad(DebugShapeComp*, GeoVector pos, GeoQuat, f32 sizeX, f32 sizeY, GeoColor, DebugShapeMode);
void debug_sphere(DebugShapeComp*, GeoVector pos, f32 radius, GeoColor, DebugShapeMode);
void debug_cylinder(DebugShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor, DebugShapeMode);
void debug_capsule(DebugShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor, DebugShapeMode);
void debug_cone(DebugShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor, DebugShapeMode);
void debug_line(DebugShapeComp*, GeoVector start, GeoVector end, GeoColor);
void debug_circle(DebugShapeComp*, GeoVector pos, GeoQuat, f32 radius, GeoColor);
void debug_arrow(DebugShapeComp*, GeoVector begin, GeoVector end, f32 radius, GeoColor);
void debug_orientation(DebugShapeComp*, GeoVector pos, GeoQuat, f32 size);
void debug_plane(DebugShapeComp*, GeoVector pos, GeoQuat, GeoColor);
void debug_frustum(DebugShapeComp*, const GeoMatrix* viewProj, GeoColor);

// clang-format on
