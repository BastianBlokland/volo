#include "scene_set.h"

ecs_comp_define_public(SceneSetMemberComp);

ecs_module_init(scene_set_module) { ecs_register_comp(SceneSetMemberComp); }
