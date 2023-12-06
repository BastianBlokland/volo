#include "scene_light.h"

ecs_comp_define_public(SceneLightPointComp);
ecs_comp_define_public(SceneLightDirComp);
ecs_comp_define_public(SceneLightAmbientComp);

ecs_module_init(scene_light_module) {
  ecs_register_comp(SceneLightPointComp);
  ecs_register_comp(SceneLightDirComp);
  ecs_register_comp(SceneLightAmbientComp);
}
