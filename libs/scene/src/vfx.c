#include "scene_vfx.h"

ecs_comp_define_public(SceneVfxComp);

ecs_module_init(scene_vfx_module) { ecs_register_comp_empty(SceneVfxComp); }
