#pragma once
#include "ecs_module.h"
#include "geo.h"
#include "geo_vector.h"

typedef enum eDevShapeMode {
  DevShape_Fill,
  DevShape_Wire,
  DevShape_Overlay,
} DevShapeMode;

ecs_comp_extern(DevShapeComp);

/**
 * Add a new debug-shape component to the given entity.
 */
DevShapeComp* dev_shape_create(EcsWorld*, EcsEntityId entity);

// clang-format off

/**
 * Draw primitives.
 */
void dev_box(DevShapeComp*, GeoVector pos, GeoQuat, GeoVector size, GeoColor, DevShapeMode);
void dev_quad(DevShapeComp*, GeoVector pos, GeoQuat, f32 sizeX, f32 sizeY, GeoColor, DevShapeMode);
void dev_sphere(DevShapeComp*, GeoVector pos, f32 radius, GeoColor, DevShapeMode);
void dev_cylinder(DevShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor, DevShapeMode);
void dev_capsule(DevShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor, DevShapeMode);
void dev_cone(DevShapeComp*, GeoVector bottom, GeoVector top, f32 radius, GeoColor, DevShapeMode);
void dev_line(DevShapeComp*, GeoVector start, GeoVector end, GeoColor);
void dev_circle(DevShapeComp*, GeoVector pos, GeoQuat, f32 radius, GeoColor);
void dev_arrow(DevShapeComp*, GeoVector begin, GeoVector end, f32 radius, GeoColor);
void dev_orientation(DevShapeComp*, GeoVector pos, GeoQuat, f32 size);
void dev_plane(DevShapeComp*, GeoVector pos, GeoQuat, GeoColor);
void dev_frustum_points(DevShapeComp*, const GeoVector points[PARAM_ARRAY_SIZE(8)], GeoColor);
void dev_frustum_matrix(DevShapeComp*, const GeoMatrix* viewProj, GeoColor);

void dev_world_box(DevShapeComp*, const GeoBox*, GeoColor);
void dev_world_box_rotated(DevShapeComp*, const GeoBoxRotated*, GeoColor);
void dev_world_sphere(DevShapeComp*, const GeoSphere*, GeoColor);
void dev_world_capsule(DevShapeComp*, const GeoCapsule*, GeoColor);

// clang-format on
