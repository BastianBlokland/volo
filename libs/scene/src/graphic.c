#include "scene_graphic.h"

ecs_comp_define_public(SceneGfxComp);

ecs_module_init(scene_gfx_module) { ecs_register_comp(SceneGfxComp); }
