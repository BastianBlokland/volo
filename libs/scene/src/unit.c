#include "ecs_world.h"
#include "scene_unit.h"

ecs_comp_define_public(SceneUnitComp);

ecs_module_init(scene_unit_module) { ecs_register_comp_empty(SceneUnitComp); }
