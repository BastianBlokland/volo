#include "ecs_world.h"
#include "scene_taunt.h"

ecs_comp_define_public(SceneTauntComp);

ecs_module_init(scene_taunt_module) { ecs_register_comp(SceneTauntComp); }
