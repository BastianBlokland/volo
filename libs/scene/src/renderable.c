#include "scene_renderable.h"

ecs_comp_define_public(SceneRenderableComp);

ecs_module_init(scene_renderable_module) { ecs_register_comp(SceneRenderableComp); }
