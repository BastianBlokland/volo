#include "scene/light.h"

ecs_comp_define(SceneLightPointComp);
ecs_comp_define(SceneLightSpotComp);
ecs_comp_define(SceneLightLineComp);
ecs_comp_define(SceneLightDirComp);
ecs_comp_define(SceneLightAmbientComp);

ecs_module_init(scene_light_module) {
  ecs_register_comp(SceneLightPointComp);
  ecs_register_comp(SceneLightSpotComp);
  ecs_register_comp(SceneLightLineComp);
  ecs_register_comp(SceneLightDirComp);
  ecs_register_comp(SceneLightAmbientComp);
}
