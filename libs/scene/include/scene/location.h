#pragma once
#include "ecs/module.h"
#include "geo/box.h"
#include "scene/forward.h"

typedef enum {
  SceneLocationType_AimTarget,

  SceneLocationType_Count,
} SceneLocationType;

ecs_comp_extern_public(SceneLocationComp) { GeoBox volumes[SceneLocationType_Count]; };

String scene_location_type_name(SceneLocationType);

GeoBoxRotated scene_location(
    const SceneLocationComp*, const SceneTransformComp*, const SceneScaleComp*, SceneLocationType);

GeoBoxRotated scene_location_predict(
    const SceneLocationComp*,
    const SceneTransformComp*,
    const SceneScaleComp*,
    const SceneVelocityComp*,
    SceneLocationType,
    TimeDuration timeInFuture);
