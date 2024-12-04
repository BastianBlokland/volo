#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_ray.h"

typedef enum {
  SceneDebugType_Line,
  SceneDebugType_Sphere,
  SceneDebugType_Box,
  SceneDebugType_Arrow,
  SceneDebugType_Orientation,
  SceneDebugType_Text,
  SceneDebugType_Trace,
} SceneDebugType;

typedef struct {
  GeoVector start, end;
  GeoColor  color;
} SceneDebugLine;

typedef struct {
  GeoVector pos;
  GeoColor  color;
  f32       radius;
} SceneDebugSphere;

typedef struct {
  GeoVector pos;
  GeoQuat   rot;
  GeoVector size;
  GeoColor  color;
} SceneDebugBox;

typedef struct {
  GeoVector start, end;
  GeoColor  color;
  f32       radius;
} SceneDebugArrow;

typedef struct {
  GeoVector pos;
  GeoQuat   rot;
  f32       size;
} SceneDebugOrientation;

typedef struct {
  GeoVector pos;
  GeoColor  color;
  String    text;
  u16       fontSize;
} SceneDebugText;

typedef struct {
  String text;
} SceneDebugTrace;

typedef struct {
  SceneDebugType type;
  union {
    SceneDebugLine        data_line;
    SceneDebugSphere      data_sphere;
    SceneDebugBox         data_box;
    SceneDebugArrow       data_arrow;
    SceneDebugOrientation data_orientation;
    SceneDebugText        data_text;
    SceneDebugTrace       data_trace;
  };
} SceneDebug;

ecs_comp_extern(SceneDebugComp);

SceneDebugComp* scene_debug_init(EcsWorld*, EcsEntityId);
void            scene_debug_push(SceneDebugComp*, SceneDebug);

const SceneDebug* scene_debug_data(const SceneDebugComp*);
usize             scene_debug_count(const SceneDebugComp*);
