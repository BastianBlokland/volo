#pragma once
#include "ecs_module.h"
#include "geo_vector.h"

// Forward declare from 'scene_transform.h'.
ecs_comp_extern(SceneTransformComp);
ecs_comp_extern(SceneScaleComp);
ecs_comp_extern(SceneVelocityComp);

// Forward declare from 'core_time.h'.
typedef i64 TimeDuration;

typedef enum {
  SceneLocationType_AimTarget,

  SceneLocationType_Count,
} SceneLocationType;

ecs_comp_extern_public(SceneLocationComp) { GeoVector offsets[SceneLocationType_Count]; };

GeoVector scene_location(
    const SceneLocationComp*, const SceneTransformComp*, const SceneScaleComp*, SceneLocationType);

GeoVector scene_location_predict(
    const SceneLocationComp*,
    const SceneTransformComp*,
    const SceneScaleComp*,
    const SceneVelocityComp*,
    SceneLocationType,
    TimeDuration);
