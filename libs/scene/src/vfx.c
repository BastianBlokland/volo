#include "scene/vfx.h"

ecs_comp_define_public(SceneVfxSystemComp);
ecs_comp_define_public(SceneVfxDecalComp);

ecs_module_init(scene_vfx_module) {
  ecs_register_comp(SceneVfxSystemComp);
  ecs_register_comp(SceneVfxDecalComp);
}
