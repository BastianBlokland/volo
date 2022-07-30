#include "scene_locomotion.h"

ecs_comp_define_public(SceneLocomotionComp);

ecs_module_init(scene_locomotion_module) { ecs_register_comp(SceneLocomotionComp); }
