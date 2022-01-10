#include "scene_renderable.h"

ecs_comp_define_public(SceneRenderableComp);
ecs_comp_define_public(SceneRenderableUniqueComp);

ecs_module_init(scene_renderable_module) {
  ecs_register_comp(SceneRenderableComp);
  ecs_register_comp(SceneRenderableUniqueComp);
}
