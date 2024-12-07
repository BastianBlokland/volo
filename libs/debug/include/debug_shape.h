#pragma once
#include "ecs_module.h"
#include "geo.h"
#include "geo_vector.h"

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
void debug_frustum_points(DebugShapeComp*, const GeoVector points[PARAM_ARRAY_SIZE(8)], GeoColor);
void debug_frustum_matrix(DebugShapeComp*, const GeoMatrix* viewProj, GeoColor);

void debug_world_box(DebugShapeComp*, const GeoBox*, GeoColor);
void debug_world_box_rotated(DebugShapeComp*, const GeoBoxRotated*, GeoColor);
void debug_world_sphere(DebugShapeComp*, const GeoSphere*, GeoColor);
void debug_world_capsule(DebugShapeComp*, const GeoCapsule*, GeoColor);

// clang-format on
