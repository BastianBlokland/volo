#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "vfx_register.h"

#include "particle_internal.h"

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

ecs_view_define(RenderGlobalView) { ecs_access_read(VfxParticleRendererComp); }
ecs_view_define(RenderDrawView) { ecs_access_write(RendDrawComp); }

ecs_view_define(RenderView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(VfxEmitterComp);
}

ecs_system_define(VfxEmitterRenderSys) {
  EcsView*     globalView = ecs_world_view_t(world, RenderGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  const VfxParticleRendererComp* renderer   = ecs_view_read_t(globalItr, VfxParticleRendererComp);
  const EcsEntityId              drawEntity = vfx_particle_draw(renderer);
  RendDrawComp* draw = ecs_utils_write_t(world, RenderDrawView, drawEntity, RendDrawComp);

  EcsView* renderView = ecs_world_view_t(world, RenderView);
  for (EcsIterator* itr = ecs_view_itr(renderView); ecs_view_walk(itr);) {
    const SceneTransformComp* transComp   = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scaleComp   = ecs_view_read_t(itr, SceneScaleComp);
    const VfxEmitterComp*     emitterComp = ecs_view_read_t(itr, VfxEmitterComp);

    const GeoVector basePos   = LIKELY(transComp) ? transComp->position : geo_vector(0);
    const GeoQuat   baseRot   = LIKELY(transComp) ? transComp->rotation : geo_quat_ident;
    const f32       baseScale = scaleComp ? scaleComp->scale : 1.0f;

    (void)emitterComp;

    vfx_particle_output(
        draw,
        &(VfxParticle){
            .position = basePos,
            .rotation = baseRot,
            .sizeX    = baseScale,
            .sizeY    = baseScale,
            .color    = geo_color(1, 0, 0, 0.5f),
        });
  }
}

ecs_module_init(vfx_emitter_module) {
  ecs_register_comp(VfxEmitterComp);

  ecs_register_system(VfxEmitterInitSys, ecs_register_view(InitView));
  ecs_register_system(VfxEmitterDeinitSys, ecs_register_view(DeinitView));

  ecs_register_system(
      VfxEmitterRenderSys,
      ecs_register_view(RenderGlobalView),
      ecs_register_view(RenderDrawView),
      ecs_register_view(RenderView));

  ecs_order(VfxEmitterRenderSys, VfxOrder_Render);
}
