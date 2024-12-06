#include "core_array.h"
#include "core_diag.h"
#include "geo_box_rotated.h"
#include "scene_location.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneLocationComp);

ecs_module_init(scene_location_module) { ecs_register_comp(SceneLocationComp); }

String scene_location_type_name(const SceneLocationType type) {
  diag_assert(type < SceneLocationType_Count);
  static const String g_names[] = {
      string_static("AimTarget"),
  };
  ASSERT(array_elems(g_names) == SceneLocationType_Count, "Incorrect number of type names");
  return g_names[type];
}

GeoBoxRotated scene_location(
    const SceneLocationComp*  locComp,
    const SceneTransformComp* transComp,
    const SceneScaleComp*     scaleComp,
    const SceneLocationType   type) {
  const f32 scale = scaleComp ? scaleComp->scale : 1.0f;
  return geo_box_rotated(&locComp->volumes[type], transComp->position, transComp->rotation, scale);
}

GeoBoxRotated scene_location_predict(
    const SceneLocationComp*  locComp,
    const SceneTransformComp* transComp,
    const SceneScaleComp*     scaleComp,
    const SceneVelocityComp*  veloComp,
    const SceneLocationType   type,
    const TimeDuration        timeInFuture) {
  const f32       scale = scaleComp ? scaleComp->scale : 1.0f;
  const GeoVector pos   = scene_position_predict(transComp, veloComp, timeInFuture);
  return geo_box_rotated(&locComp->volumes[type], pos, transComp->rotation, scale);
}
