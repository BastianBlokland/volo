#include "scene_creator.h"

ecs_comp_define_public(SceneCreatorComp);

ecs_module_init(scene_creator_module) { ecs_register_comp(SceneCreatorComp); }
