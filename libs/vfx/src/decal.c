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
#include "decal_internal.h"

#define vfx_decal_max_asset_requests 4

// clang-format off
static const String        g_vfxDecalGraphic   = string_static("graphics/vfx/decal.gra");
static const RendDrawFlags g_vfxDecalDrawFlags = /* RendDrawFlags_Decal | */ RendDrawFlags_Preload;
// clang-format on

typedef struct {
  ALIGNAS(16)
  GeoVector pos;
  GeoQuat   rot;
  GeoVector scale;
} VfxDecalData;

ASSERT(sizeof(VfxDecalData) == 48, "Size needs to match the size defined in glsl");
ASSERT(alignof(VfxDecalData) == 16, "Alignment needs to match the glsl alignment");

typedef enum {
  VfxLoad_Acquired  = 1 << 0,
  VfxLoad_Unloading = 1 << 1,
} VfxLoadFlags;

ecs_comp_define(VfxDecalRendererComp) { EcsEntityId drawEntity; };

ecs_comp_define(VfxDecalDrawComp);

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

ecs_view_define(AtlasView) { ecs_access_read(AssetAtlasComp); }

ecs_view_define(DecalDrawView) {
  ecs_access_with(VfxDecalDrawComp);
  ecs_access_write(RendDrawComp);
}

ecs_view_define(DecalInstanceView) { ecs_access_read(VfxDecalInstanceComp); }

static const AssetAtlasComp*
vfx_atlas(EcsWorld* world, const VfxAtlasManagerComp* manager, const VfxAtlasType type) {
  const EcsEntityId atlasEntity = vfx_atlas_entity(manager, type);
  EcsIterator*      itr = ecs_view_maybe_at(ecs_world_view_t(world, AtlasView), atlasEntity);
  return LIKELY(itr) ? ecs_view_read_t(itr, AssetAtlasComp) : null;
}

static EcsEntityId vfx_decal_draw_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId entity = asset_lookup(world, assets, g_vfxDecalGraphic);
  ecs_world_add_empty_t(world, entity, VfxDecalDrawComp);
  RendDrawComp* draw = rend_draw_create(world, entity, g_vfxDecalDrawFlags);
  rend_draw_set_graphic(draw, entity); // Graphic is on the same entity as the draw.
  return entity;
}

ecs_view_define(RendererInitGlobalView) {
  ecs_access_maybe_write(VfxDecalRendererComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(VfxDecalRendererInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, RendererInitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (UNLIKELY(!globalItr)) {
    return;
  }
  AssetManagerComp*     assets   = ecs_view_write_t(globalItr, AssetManagerComp);
  VfxDecalRendererComp* renderer = ecs_view_write_t(globalItr, VfxDecalRendererComp);

  if (UNLIKELY(!renderer)) {
    renderer             = ecs_world_add_t(world, ecs_world_global(world), VfxDecalRendererComp);
    renderer->drawEntity = vfx_decal_draw_create(world, assets);
  }
}

ecs_view_define(AssetLoadView) { ecs_access_write(VfxDecalAssetComp); }

static void vfx_decal_instance_reset_all(EcsWorld* world) {
  EcsView* instanceView = ecs_world_view_t(world, DecalInstanceView);
  for (EcsIterator* itr = ecs_view_itr(instanceView); ecs_view_walk(itr);) {
    ecs_world_remove_t(world, ecs_view_entity(itr), VfxDecalInstanceComp);
  }
}

ecs_system_define(VfxDecalAssetLoadSys) {
  EcsView* loadView = ecs_world_view_t(world, AssetLoadView);

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

ecs_view_define(InstanceInitGlobalView) { ecs_access_read(VfxAtlasManagerComp); }

ecs_view_define(InstanceInitView) {
  ecs_access_read(SceneVfxDecalComp);
  ecs_access_without(VfxDecalInstanceComp);
}

ecs_view_define(InstanceInitAssetView) {
  ecs_access_with(VfxDecalAssetComp);
  ecs_access_read(AssetDecalComp);
}

ecs_system_define(VfxDecalInstanceInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, InstanceInitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const VfxAtlasManagerComp* atlasManager = ecs_view_read_t(globalItr, VfxAtlasManagerComp);
  const AssetAtlasComp*      colorAtlas   = vfx_atlas(world, atlasManager, VfxAtlasType_DecalColor);
  if (!colorAtlas) {
    return; // Atlas hasn't loaded yet.
  }

  EcsIterator* assetItr         = ecs_view_itr(ecs_world_view_t(world, InstanceInitAssetView));
  u32          numAssetRequests = 0;

  EcsView* initView = ecs_world_view_t(world, InstanceInitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId        e     = ecs_view_entity(itr);
    const SceneVfxDecalComp* decal = ecs_view_read_t(itr, SceneVfxDecalComp);

    diag_assert_msg(ecs_entity_valid(decal->asset), "Vfx decal is missing an asset");
    if (!ecs_view_maybe_jump(assetItr, decal->asset)) {
      if (decal->asset && ++numAssetRequests < vfx_decal_max_asset_requests) {
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

ecs_view_define(InstanceDeinitView) {
  ecs_access_with(VfxDecalInstanceComp);
  ecs_access_without(SceneVfxDecalComp);
}

ecs_system_define(VfxDecalInstanceDeinitSys) {
  EcsView* deinitView = ecs_world_view_t(world, InstanceDeinitView);
  for (EcsIterator* itr = ecs_view_itr(deinitView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, VfxDecalInstanceComp);
  }
}

ecs_view_define(InstanceUpdateGlobalView) { ecs_access_read(VfxDecalRendererComp); }

ecs_view_define(InstanceUpdateView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(VfxDecalInstanceComp);
}

ecs_system_define(VfxDecalInstanceUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, InstanceUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const VfxDecalRendererComp* rend = ecs_view_read_t(globalItr, VfxDecalRendererComp);
  RendDrawComp* decalDraw = ecs_utils_write_t(world, DecalDrawView, rend->drawEntity, RendDrawComp);

  EcsView* updateView = ecs_world_view_t(world, InstanceUpdateView);
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
    data->pos          = pos;
    data->rot          = rot;
    data->scale        = size;
  }
}

ecs_module_init(vfx_decal_module) {
  ecs_register_comp(VfxDecalRendererComp);
  ecs_register_comp_empty(VfxDecalDrawComp);
  ecs_register_comp(VfxDecalInstanceComp);
  ecs_register_comp(VfxDecalAssetComp, .combinator = ecs_combine_decal_asset);

  ecs_register_view(AtlasView);
  ecs_register_view(DecalDrawView);
  ecs_register_view(DecalInstanceView);

  ecs_register_system(VfxDecalRendererInitSys, ecs_register_view(RendererInitGlobalView));

  ecs_register_system(
      VfxDecalAssetLoadSys, ecs_register_view(AssetLoadView), ecs_view_id(DecalInstanceView));

  ecs_register_system(
      VfxDecalInstanceInitSys,
      ecs_register_view(InstanceInitGlobalView),
      ecs_register_view(InstanceInitView),
      ecs_register_view(InstanceInitAssetView),
      ecs_view_id(AtlasView));

  ecs_register_system(VfxDecalInstanceDeinitSys, ecs_register_view(InstanceDeinitView));

  ecs_register_system(
      VfxDecalInstanceUpdateSys,
      ecs_register_view(InstanceUpdateGlobalView),
      ecs_register_view(InstanceUpdateView),
      ecs_view_id(DecalDrawView));

  ecs_order(VfxDecalInstanceUpdateSys, VfxOrder_Update);
}
