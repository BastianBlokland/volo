#include "ecs_world.h"
#include "scene_footstep.h"

ecs_comp_define_public(SceneFootstepComp);

ecs_module_init(scene_footstep_module) { ecs_register_comp(SceneFootstepComp); }
