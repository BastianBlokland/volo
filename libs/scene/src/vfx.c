#include "scene/vfx.h"

ecs_comp_define(SceneVfxSystemComp);
ecs_comp_define(SceneVfxDecalComp);

ecs_module_init(scene_vfx_module) {
  ecs_register_comp(SceneVfxSystemComp);
  ecs_register_comp(SceneVfxDecalComp);
}
