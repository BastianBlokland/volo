#pragma once
#include "ecs_module.h"
#include "geo.h"
#include "geo_vector.h"

typedef enum eDebugShapeMode {
  DebugShape_Fill,
  DebugShape_Wire,
  DebugShape_Overlay,
} DebugShapeMode;

ecs_comp_extern(DevShapeComp);

/**
 * Add a new debug-shape component to the given entity.
 */
DevShapeComp* debug_shape_create(EcsWorld*, EcsEntityId entity);

// clang-format off

/**
 * Draw primitives.
 */
void debug_box(DevShapeComp*, GeoVector pos, GeoQuat, GeoVector size, GeoColor, DebugShapeMode);
void debug_quad(DevShapeComp*, GeoVector pos, GeoQuat, f32 sizeX, f32 sizeY, GeoColor, DebugShapeMode);
void debug_sphere(DevShapeComp*, GeoVector pos, f32 radius, GeoColor, DebugShapeMode);
void debug_cylinder(DevShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor, DebugShapeMode);
void debug_capsule(DevShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor, DebugShapeMode);
void debug_cone(DevShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor, DebugShapeMode);
void debug_line(DevShapeComp*, GeoVector start, GeoVector end, GeoColor);
void debug_circle(DevShapeComp*, GeoVector pos, GeoQuat, f32 radius, GeoColor);
void debug_arrow(DevShapeComp*, GeoVector begin, GeoVector end, f32 radius, GeoColor);
void debug_orientation(DevShapeComp*, GeoVector pos, GeoQuat, f32 size);
void debug_plane(DevShapeComp*, GeoVector pos, GeoQuat, GeoColor);
void debug_frustum_points(DevShapeComp*, const GeoVector points[PARAM_ARRAY_SIZE(8)], GeoColor);
void debug_frustum_matrix(DevShapeComp*, const GeoMatrix* viewProj, GeoColor);

void debug_world_box(DevShapeComp*, const GeoBox*, GeoColor);
void debug_world_box_rotated(DevShapeComp*, const GeoBoxRotated*, GeoColor);
void debug_world_sphere(DevShapeComp*, const GeoSphere*, GeoColor);
void debug_world_capsule(DevShapeComp*, const GeoCapsule*, GeoColor);

// clang-format on
