#include "asset_atlas.h"
#include "asset_decal.h"
#include "asset_manager.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_draw.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "vfx_register.h"

#include "atlas_internal.h"
#include "draw_internal.h"

#define vfx_decal_max_asset_requests 4

typedef struct {
  VfxAtlasDrawData atlas;
} VfxDecalMetaData;

ASSERT(sizeof(VfxDecalMetaData) == 16, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(16)
  f32 data1[4]; // xyz: position, w: atlasIndex.
  f32 data2[4]; // xyzw: rotation quaternion.
  f32 data3[4]; // xyz: scale
} VfxDecalData;

ASSERT(sizeof(VfxDecalData) == 48, "Size needs to match the size defined in glsl");

typedef enum {
  VfxLoad_Acquired  = 1 << 0,
  VfxLoad_Unloading = 1 << 1,
} VfxLoadFlags;

ecs_comp_define(VfxDecalInstanceComp) {
  u16       colorAtlasIndex;
  GeoVector size;
};

ecs_comp_define(VfxDecalAssetComp) { VfxLoadFlags loadFlags; };

static void ecs_combine_decal_asset(void* dataA, void* dataB) {
  VfxDecalAssetComp* compA = dataA;
  VfxDecalAssetComp* compB = dataB;
  compA->loadFlags |= compB->loadFlags;
}

ecs_view_define(GlobalView) {
  ecs_access_read(VfxAtlasManagerComp);
  ecs_access_read(VfxDrawManagerComp);
}

ecs_view_define(AtlasView) { ecs_access_read(AssetAtlasComp); }

ecs_view_define(DecalDrawView) {
  ecs_access_with(VfxDrawDecalComp);
  ecs_access_write(RendDrawComp);
}

ecs_view_define(DecalInstanceView) { ecs_access_read(VfxDecalInstanceComp); }

static const AssetAtlasComp*
vfx_atlas(EcsWorld* world, const VfxAtlasManagerComp* manager, const VfxAtlasType type) {
  const EcsEntityId atlasEntity = vfx_atlas_entity(manager, type);
  EcsIterator*      itr = ecs_view_maybe_at(ecs_world_view_t(world, AtlasView), atlasEntity);
  return LIKELY(itr) ? ecs_view_read_t(itr, AssetAtlasComp) : null;
}

ecs_view_define(LoadView) { ecs_access_write(VfxDecalAssetComp); }

static void vfx_decal_instance_reset_all(EcsWorld* world) {
  EcsView* instanceView = ecs_world_view_t(world, DecalInstanceView);
  for (EcsIterator* itr = ecs_view_itr(instanceView); ecs_view_walk(itr);) {
    ecs_world_remove_t(world, ecs_view_entity(itr), VfxDecalInstanceComp);
  }
}

ecs_system_define(VfxDecalLoadSys) {
  EcsView* loadView = ecs_world_view_t(world, LoadView);

  bool decalUnloaded = false;
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity     = ecs_view_entity(itr);
    VfxDecalAssetComp* request    = ecs_view_write_t(itr, VfxDecalAssetComp);
    const bool         isLoaded   = ecs_world_has_t(world, entity, AssetLoadedComp);
    const bool         isFailed   = ecs_world_has_t(world, entity, AssetFailedComp);
    const bool         hasChanged = ecs_world_has_t(world, entity, AssetChangedComp);

    if (request->loadFlags & VfxLoad_Acquired && (isLoaded || isFailed) && hasChanged) {
      asset_release(world, entity);
      request->loadFlags &= ~VfxLoad_Acquired;
      request->loadFlags |= VfxLoad_Unloading;
    }
    if (request->loadFlags & VfxLoad_Unloading && !isLoaded) {
      request->loadFlags &= ~VfxLoad_Unloading;
      decalUnloaded = true;
    }
    if (!(request->loadFlags & (VfxLoad_Acquired | VfxLoad_Unloading))) {
      asset_acquire(world, entity);
      request->loadFlags |= VfxLoad_Acquired;
    }
  }

  if (decalUnloaded) {
    // TODO: Only reset decals whose asset was actually unloaded.
    vfx_decal_instance_reset_all(world);
  }
}

static bool vfx_decal_asset_request(EcsWorld* world, const EcsEntityId assetEntity) {
  if (!ecs_world_has_t(world, assetEntity, VfxDecalAssetComp)) {
    ecs_world_add_t(world, assetEntity, VfxDecalAssetComp);
    return true;
  }
  return false;
}

ecs_view_define(InitView) {
  ecs_access_read(SceneVfxDecalComp);
  ecs_access_without(VfxDecalInstanceComp);
}

ecs_view_define(InitAssetView) {
  ecs_access_with(VfxDecalAssetComp);
  ecs_access_read(AssetDecalComp);
}

ecs_system_define(VfxDecalInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const VfxAtlasManagerComp* atlasManager = ecs_view_read_t(globalItr, VfxAtlasManagerComp);
  const AssetAtlasComp*      colorAtlas   = vfx_atlas(world, atlasManager, VfxAtlasType_DecalColor);
  if (!colorAtlas) {
    return; // Atlas hasn't loaded yet.
  }

  EcsIterator* assetItr         = ecs_view_itr(ecs_world_view_t(world, InitAssetView));
  u32          numAssetRequests = 0;

  EcsView* initView = ecs_world_view_t(world, InitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId        e     = ecs_view_entity(itr);
    const SceneVfxDecalComp* decal = ecs_view_read_t(itr, SceneVfxDecalComp);

    diag_assert_msg(ecs_entity_valid(decal->asset), "Vfx decal is missing an asset");
    if (!ecs_view_maybe_jump(assetItr, decal->asset)) {
      if (++numAssetRequests < vfx_decal_max_asset_requests) {
        vfx_decal_asset_request(world, decal->asset);
      }
      continue;
    }
    const AssetDecalComp*  asset           = ecs_view_read_t(assetItr, AssetDecalComp);
    const AssetAtlasEntry* colorAtlasEntry = asset_atlas_lookup(colorAtlas, asset->colorAtlasEntry);
    if (UNLIKELY(!colorAtlasEntry)) {
      log_e(
          "Vfx decal color-atlas entry missing",
          log_param("entry-hash", fmt_int(asset->colorAtlasEntry)));
      continue;
    }
    ecs_world_add_t(
        world,
        e,
        VfxDecalInstanceComp,
        .colorAtlasIndex = colorAtlasEntry->atlasIndex,
        .size            = geo_vector(asset->width, asset->thickness, asset->height));
  }
}

ecs_view_define(DeinitView) {
  ecs_access_with(VfxDecalInstanceComp);
  ecs_access_without(SceneVfxDecalComp);
}

ecs_system_define(VfxDecalDeinitSys) {
  EcsView* deinitView = ecs_world_view_t(world, DeinitView);
  for (EcsIterator* itr = ecs_view_itr(deinitView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, VfxDecalInstanceComp);
  }
}

ecs_view_define(UpdateView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(VfxDecalInstanceComp);
}

static void vfx_decal_draw_init(RendDrawComp* draw, const AssetAtlasComp* atlas) {
  *rend_draw_set_data_t(draw, VfxDecalMetaData) = (VfxDecalMetaData){
      .atlas = vfx_atlas_draw_data(atlas),
  };
}

ecs_system_define(VfxDecalUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  const VfxAtlasManagerComp* atlasManager = ecs_view_read_t(globalItr, VfxAtlasManagerComp);
  const AssetAtlasComp*      colorAtlas   = vfx_atlas(world, atlasManager, VfxAtlasType_DecalColor);
  if (!colorAtlas) {
    return; // Atlas hasn't loaded yet.
  }

  const VfxDrawManagerComp* drawManager     = ecs_view_read_t(globalItr, VfxDrawManagerComp);
  const EcsEntityId         decalDrawEntity = vfx_draw_entity(drawManager, VfxDrawType_Decal);
  RendDrawComp* decalDraw = ecs_utils_write_t(world, DecalDrawView, decalDrawEntity, RendDrawComp);

  vfx_decal_draw_init(decalDraw, colorAtlas);

  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const SceneScaleComp*       scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const SceneTransformComp*   transComp = ecs_view_read_t(itr, SceneTransformComp);
    const VfxDecalInstanceComp* instance  = ecs_view_read_t(itr, VfxDecalInstanceComp);

    const GeoVector pos    = LIKELY(transComp) ? transComp->position : geo_vector(0);
    const GeoQuat   rot    = LIKELY(transComp) ? transComp->rotation : geo_quat_ident;
    const f32       scale  = scaleComp ? scaleComp->scale : 1.0f;
    const GeoVector size   = geo_vector_mul(instance->size, scale);
    const GeoBox    box    = geo_box_from_center(pos, size);
    const GeoBox    bounds = geo_box_from_rotated(&box, rot);

    VfxDecalData* data = rend_draw_add_instance_t(decalDraw, VfxDecalData, SceneTags_Vfx, bounds);
    data->data1[0]     = pos.x;
    data->data1[1]     = pos.y;
    data->data1[2]     = pos.z;
    data->data1[3]     = (f32)instance->colorAtlasIndex;
    data->data2[0]     = rot.x;
    data->data2[1]     = rot.y;
    data->data2[2]     = rot.z;
    data->data2[3]     = rot.w;
    data->data3[0]     = size.x;
    data->data3[1]     = size.y;
    data->data3[2]     = size.z;
  }
}

ecs_module_init(vfx_decal_module) {
  ecs_register_comp(VfxDecalInstanceComp);
  ecs_register_comp(VfxDecalAssetComp, .combinator = ecs_combine_decal_asset);

  ecs_register_view(GlobalView);
  ecs_register_view(AtlasView);
  ecs_register_view(DecalDrawView);
  ecs_register_view(DecalInstanceView);

  ecs_register_system(VfxDecalLoadSys, ecs_register_view(LoadView), ecs_view_id(DecalInstanceView));

  ecs_register_system(
      VfxDecalInitSys,
      ecs_register_view(InitView),
      ecs_register_view(InitAssetView),
      ecs_view_id(AtlasView),
      ecs_view_id(GlobalView));

  ecs_register_system(VfxDecalDeinitSys, ecs_register_view(DeinitView));

  ecs_register_system(
      VfxDecalUpdateSys,
      ecs_register_view(UpdateView),
      ecs_view_id(DecalDrawView),
      ecs_view_id(AtlasView),
      ecs_view_id(GlobalView));

  ecs_order(VfxDecalUpdateSys, VfxOrder_Update);
}
