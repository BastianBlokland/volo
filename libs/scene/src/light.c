#include "scene/light.h"

ecs_comp_define_public(SceneLightPointComp);
ecs_comp_define_public(SceneLightSpotComp);
ecs_comp_define_public(SceneLightLineComp);
ecs_comp_define_public(SceneLightDirComp);
ecs_comp_define_public(SceneLightAmbientComp);

ecs_module_init(scene_light_module) {
  ecs_register_comp(SceneLightPointComp);
  ecs_register_comp(SceneLightSpotComp);
  ecs_register_comp(SceneLightLineComp);
  ecs_register_comp(SceneLightDirComp);
  ecs_register_comp(SceneLightAmbientComp);
}
