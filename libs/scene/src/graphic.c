#include "scene_graphic.h"

ecs_comp_define_public(SceneGraphicComp);

ecs_module_init(scene_graphic_module) { ecs_register_comp(SceneGraphicComp); }
