#include "ecs_world.h"
#include "scene_visibility.h"

ecs_comp_define(SceneVisibilityEnvComp) { u32 dummy; };

ecs_comp_define_public(SceneVisionComp);

ecs_module_init(scene_visibility_module) {
  ecs_register_comp(SceneVisibilityEnvComp);
  ecs_register_comp(SceneVisionComp);
}
