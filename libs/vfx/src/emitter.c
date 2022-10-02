#include "ecs_world.h"
#include "scene_vfx.h"
#include "vfx_register.h"

ecs_comp_define(VfxEmitterComp) { u32 dummy; };

ecs_view_define(InitView) {
  ecs_access_read(SceneVfxComp);
  ecs_access_without(VfxEmitterComp);
}

ecs_system_define(VfxEmitterInitSys) {
  EcsView* initView = ecs_world_view_t(world, InitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_add_t(world, entity, VfxEmitterComp);
  }
}

ecs_view_define(DeinitView) {
  ecs_access_with(VfxEmitterComp);
  ecs_access_without(SceneVfxComp);
}

ecs_system_define(VfxEmitterDeinitSys) {
  EcsView* deinitView = ecs_world_view_t(world, DeinitView);
  for (EcsIterator* itr = ecs_view_itr(deinitView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, VfxEmitterComp);
  }
}

ecs_module_init(vfx_emitter_module) {
  ecs_register_comp(VfxEmitterComp);

  ecs_register_system(VfxEmitterInitSys, ecs_register_view(InitView));
  ecs_register_system(VfxEmitterDeinitSys, ecs_register_view(DeinitView));
}
