#include "asset_atlas.h"
#include "asset_manager.h"
#include "asset_vfx.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "vfx_register.h"

#include "particle_internal.h"

#define vfx_max_asset_requests 16

ecs_comp_define(VfxSystemComp) { u32 dummy; };

typedef enum {
  VfxAsset_Acquired  = 1 << 0,
  VfxAsset_Unloading = 1 << 1,
} VfxAssetRequestFlags;

ecs_comp_define(VfxAssetRequestComp) { VfxAssetRequestFlags flags; };

static void ecs_combine_asset_request(void* dataA, void* dataB) {
  VfxAssetRequestComp* compA = dataA;
  VfxAssetRequestComp* compB = dataB;
  compA->flags |= compB->flags;
}

ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }
ecs_view_define(AtlasView) { ecs_access_read(AssetAtlasComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetVfxComp); }

static const AssetAtlasComp* vfx_atlas(EcsWorld* world, const EcsEntityId atlasEntity) {
  EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(world, AtlasView), atlasEntity);
  return LIKELY(itr) ? ecs_view_read_t(itr, AssetAtlasComp) : null;
}

static bool vfx_asset_request(EcsWorld* world, const EcsEntityId assetEntity) {
  if (!ecs_world_has_t(world, assetEntity, VfxAssetRequestComp)) {
    ecs_world_add_t(world, assetEntity, VfxAssetRequestComp);
    return true;
  }
  return false;
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

ecs_view_define(LoadView) { ecs_access_write(VfxAssetRequestComp); }

ecs_system_define(VfxAssetLoadSys) {
  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId    entity     = ecs_view_entity(itr);
    VfxAssetRequestComp* request    = ecs_view_write_t(itr, VfxAssetRequestComp);
    const bool           isLoaded   = ecs_world_has_t(world, entity, AssetLoadedComp);
    const bool           isFailed   = ecs_world_has_t(world, entity, AssetFailedComp);
    const bool           hasChanged = ecs_world_has_t(world, entity, AssetChangedComp);

    if (request->flags & VfxAsset_Acquired && (isLoaded || isFailed) && hasChanged) {
      asset_release(world, entity);
      request->flags &= ~VfxAsset_Acquired;
      request->flags |= VfxAsset_Unloading;
    }
    if (request->flags & VfxAsset_Unloading && !isLoaded) {
      request->flags &= ~VfxAsset_Unloading;
    }
    if (!(request->flags & (VfxAsset_Acquired | VfxAsset_Unloading))) {
      asset_acquire(world, entity);
      request->flags |= VfxAsset_Acquired;
    }
  }
}

ecs_view_define(RenderGlobalView) { ecs_access_read(VfxParticleRendererComp); }

ecs_view_define(RenderView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(SceneVfxComp);
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

  EcsIterator* assetItr = ecs_view_itr(ecs_world_view_t(world, AssetView));

  u32 numAssetRequests = 0;

  EcsView* renderView = ecs_world_view_t(world, RenderView);
  for (EcsIterator* itr = ecs_view_itr(renderView); ecs_view_walk(itr);) {
    const SceneTransformComp* transComp = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const SceneVfxComp*       vfxComp   = ecs_view_read_t(itr, SceneVfxComp);
    const VfxSystemComp*      sysComp   = ecs_view_read_t(itr, VfxSystemComp);

    const GeoVector basePos   = LIKELY(transComp) ? transComp->position : geo_vector(0);
    const GeoQuat   baseRot   = LIKELY(transComp) ? transComp->rotation : geo_quat_ident;
    const f32       baseScale = scaleComp ? scaleComp->scale : 1.0f;

    (void)sysComp;

    if (!ecs_view_maybe_jump(assetItr, vfxComp->asset)) {
      if (++numAssetRequests < vfx_max_asset_requests) {
        vfx_asset_request(world, vfxComp->asset);
      }
      continue;
    }
    const AssetVfxComp*    asset      = ecs_view_read_t(assetItr, AssetVfxComp);
    const AssetAtlasEntry* atlasEntry = asset_atlas_lookup(atlas, asset->atlasEntry);
    if (UNLIKELY(!atlasEntry)) {
      log_w("Vfx asset entry missing", log_param("atlas-entry-hash", fmt_int(asset->atlasEntry)));
      continue;
    }
    const GeoVector pos = geo_vector_add(
        basePos, geo_quat_rotate(baseRot, geo_vector_mul(asset->position, baseScale)));
    const GeoQuat rot = geo_quat_mul(baseRot, asset->rotation);

    vfx_particle_output(
        draw,
        &(VfxParticle){
            .position   = pos,
            .rotation   = rot,
            .atlasIndex = atlasEntry->atlasIndex,
            .sizeX      = baseScale * asset->sizeX,
            .sizeY      = baseScale * asset->sizeY,
            .color      = asset->color,
        });
  }
}

ecs_module_init(vfx_system_module) {
  ecs_register_comp(VfxSystemComp);
  ecs_register_comp(VfxAssetRequestComp, .combinator = ecs_combine_asset_request);

  ecs_register_view(DrawView);
  ecs_register_view(AssetView);
  ecs_register_view(AtlasView);

  ecs_register_system(VfxSystemInitSys, ecs_register_view(InitView));
  ecs_register_system(VfxSystemDeinitSys, ecs_register_view(DeinitView));

  ecs_register_system(VfxAssetLoadSys, ecs_register_view(LoadView));

  ecs_register_system(
      VfxSystemRenderSys,
      ecs_register_view(RenderGlobalView),
      ecs_register_view(RenderView),
      ecs_view_id(DrawView),
      ecs_view_id(AssetView),
      ecs_view_id(AtlasView));

  ecs_order(VfxSystemRenderSys, VfxOrder_Render);
}
