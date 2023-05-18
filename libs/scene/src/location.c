#include "ecs_world.h"
#include "scene_location.h"

ecs_comp_define_public(SceneLocationComp);

ecs_module_init(scene_location_module) { ecs_register_comp(SceneLocationComp); }
