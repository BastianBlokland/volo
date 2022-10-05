#include "asset_atlas.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "vfx_register.h"

#include "particle_internal.h"

ecs_comp_define(VfxSystemComp) { u32 dummy; };

ecs_view_define(AtlasView) { ecs_access_read(AssetAtlasComp); }
ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

static const AssetAtlasComp* vfx_atlas(EcsWorld* world, const EcsEntityId entity) {
  EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(world, AtlasView), entity);
  return itr ? ecs_view_read_t(itr, AssetAtlasComp) : null;
}

ecs_view_define(InitView) {
  ecs_access_read(SceneVfxComp);
  ecs_access_without(VfxSystemComp);
}

ecs_system_define(VfxSystemInitSys) {
  EcsView* initView = ecs_world_view_t(world, InitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_add_t(world, entity, VfxSystemComp);
  }
}

ecs_view_define(DeinitView) {
  ecs_access_with(VfxSystemComp);
  ecs_access_without(SceneVfxComp);
}

ecs_system_define(VfxSystemDeinitSys) {
  EcsView* deinitView = ecs_world_view_t(world, DeinitView);
  for (EcsIterator* itr = ecs_view_itr(deinitView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, VfxSystemComp);
  }
}

ecs_view_define(RenderGlobalView) { ecs_access_read(VfxParticleRendererComp); }

ecs_view_define(RenderView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(VfxSystemComp);
}

ecs_system_define(VfxSystemRenderSys) {
  EcsView*     globalView = ecs_world_view_t(world, RenderGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  const VfxParticleRendererComp* rend = ecs_view_read_t(globalItr, VfxParticleRendererComp);
  RendDrawComp* draw = ecs_utils_write_t(world, DrawView, vfx_particle_draw(rend), RendDrawComp);

  const AssetAtlasComp* atlas = vfx_atlas(world, vfx_particle_atlas(rend));
  if (!atlas) {
    return; // Atlas hasn't loaded yet.
  }

  vfx_particle_init(draw, atlas);

  EcsView* renderView = ecs_world_view_t(world, RenderView);
  for (EcsIterator* itr = ecs_view_itr(renderView); ecs_view_walk(itr);) {
    const SceneTransformComp* transComp = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const VfxSystemComp*      sysComp   = ecs_view_read_t(itr, VfxSystemComp);

    const GeoVector basePos   = LIKELY(transComp) ? transComp->position : geo_vector(0);
    const GeoQuat   baseRot   = LIKELY(transComp) ? transComp->rotation : geo_quat_ident;
    const f32       baseScale = scaleComp ? scaleComp->scale : 1.0f;

    (void)sysComp;

    vfx_particle_output(
        draw,
        &(VfxParticle){
            .position   = basePos,
            .rotation   = baseRot,
            .atlasIndex = 0,
            .sizeX      = baseScale,
            .sizeY      = baseScale,
            .color      = geo_color(1, 0, 0, 0.5f),
        });
  }
}

ecs_module_init(vfx_system_module) {
  ecs_register_comp(VfxSystemComp);

  ecs_register_view(DrawView);
  ecs_register_view(AtlasView);

  ecs_register_system(VfxSystemInitSys, ecs_register_view(InitView));
  ecs_register_system(VfxSystemDeinitSys, ecs_register_view(DeinitView));

  ecs_register_system(
      VfxSystemRenderSys,
      ecs_register_view(RenderGlobalView),
      ecs_register_view(RenderView),
      ecs_view_id(DrawView),
      ecs_view_id(AtlasView));

  ecs_order(VfxSystemRenderSys, VfxOrder_Render);
}
