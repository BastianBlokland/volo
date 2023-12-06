#include "scene_light.h"

ecs_comp_define_public(SceneLightPointComp);

ecs_module_init(scene_light_module) { ecs_register_comp(SceneLightPointComp); }
