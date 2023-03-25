#include "asset_atlas.h"
#include "asset_decal.h"
#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_draw.h"
#include "scene_tag.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "vfx_register.h"

#include "atlas_internal.h"
#include "draw_internal.h"

#define vfx_decal_max_asset_requests 4

typedef struct {
  VfxAtlasDrawData atlasColor, atlasNormal;
} VfxDecalMetaData;

ASSERT(sizeof(VfxDecalMetaData) == 32, "Size needs to match the size defined in glsl");

/**
 * NOTE: Flag values are used in GLSL, update the GLSL side when changing these.
 */
typedef enum {
  VfxDecal_NoColorOutput         = 1 << 0, // Disable color output.
  VfxDecal_NormalMap             = 1 << 1, // Output custom normals to the gbuffer.
  VfxDecal_GBufferBaseNormal     = 1 << 2, // Use the current gbuffer normal as the base normal.
  VfxDecal_DepthBufferBaseNormal = 1 << 3, // Compute the base normal from the depth buffer.
  VfxDecal_FadeUsingDepthNormal  = 1 << 4, // Angle fade using depth-buffer instead of gbuffer nrm.
} VfxDecalFlags;

typedef struct {
  ALIGNAS(16)
  f32 data1[4]; // xyz: position, w: flags.
  f16 data2[4]; // xyzw: rotation quaternion.
  f16 data3[4]; // xyz: scale, w: unused.
  f16 data4[4]; // x: atlasColorIndex, x: atlasNormalIndex, y: roughness, w: alpha.
} VfxDecalData;

ASSERT(sizeof(VfxDecalData) == 48, "Size needs to match the size defined in glsl");

typedef enum {
  VfxLoad_Acquired  = 1 << 0,
  VfxLoad_Unloading = 1 << 1,
} VfxLoadFlags;

ecs_comp_define(VfxDecalInstanceComp) {
  u16            atlasColorIndex, atlasNormalIndex;
  VfxDecalFlags  flags : 16;
  AssetDecalAxis projectionAxis : 8;
  f32            roughness;
  f32            alpha;
  GeoVector      size;
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

static VfxDecalFlags vfx_decal_flags(const AssetDecalComp* decalAsset) {
  VfxDecalFlags flags = 0;
  if (decalAsset->noColorOutput) {
    flags |= VfxDecal_NoColorOutput;
  }
  if (decalAsset->normalAtlasEntry) {
    flags |= VfxDecal_NormalMap;
  }
  switch (decalAsset->baseNormal) {
  case AssetDecalNormal_GBuffer:
    flags |= VfxDecal_GBufferBaseNormal;
    break;
  case AssetDecalNormal_DepthBuffer:
    flags |= VfxDecal_DepthBufferBaseNormal;
    break;
  case AssetDecalNormal_DecalTransform:
    // DecalTransform as the base-normal is the default.
    break;
  }
  if (decalAsset->fadeUsingDepthNormal) {
    flags |= VfxDecal_FadeUsingDepthNormal;
  }
  return flags;
}

ecs_system_define(VfxDecalInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const VfxAtlasManagerComp* atlasManager = ecs_view_read_t(globalItr, VfxAtlasManagerComp);
  const AssetAtlasComp*      atlasColor   = vfx_atlas(world, atlasManager, VfxAtlasType_DecalColor);
  const AssetAtlasComp*      atlasNormal = vfx_atlas(world, atlasManager, VfxAtlasType_DecalNormal);
  if (!atlasColor || !atlasNormal) {
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
    const AssetDecalComp* asset           = ecs_view_read_t(assetItr, AssetDecalComp);
    u16                   atlasColorIndex = 0, atlasNormalIndex = 0;
    {
      const AssetAtlasEntry* entry = asset_atlas_lookup(atlasColor, asset->colorAtlasEntry);
      if (UNLIKELY(!entry)) {
        log_e("Vfx decal color-atlas entry missing");
        continue;
      }
      atlasColorIndex = entry->atlasIndex;
    }
    if (asset->normalAtlasEntry) {
      const AssetAtlasEntry* entry = asset_atlas_lookup(atlasNormal, asset->normalAtlasEntry);
      if (UNLIKELY(!entry)) {
        log_e("Vfx decal normal-atlas entry missing");
        continue;
      }
      atlasNormalIndex = entry->atlasIndex;
    }
    ecs_world_add_t(
        world,
        e,
        VfxDecalInstanceComp,
        .atlasColorIndex  = atlasColorIndex,
        .atlasNormalIndex = atlasNormalIndex,
        .flags            = vfx_decal_flags(asset),
        .projectionAxis   = asset->projectionAxis,
        .size             = geo_vector(asset->width, asset->height, asset->thickness),
        .roughness        = asset->roughness,
        .alpha            = asset->alpha);
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
  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(SceneVfxDecalComp);
  ecs_access_read(VfxDecalInstanceComp);
}

static void vfx_decal_draw_init(
    RendDrawComp* draw, const AssetAtlasComp* atlasColor, const AssetAtlasComp* atlasNormal) {
  *rend_draw_set_data_t(draw, VfxDecalMetaData) = (VfxDecalMetaData){
      .atlasColor  = vfx_atlas_draw_data(atlasColor),
      .atlasNormal = vfx_atlas_draw_data(atlasNormal),
  };
}

static RendDrawComp*
vfx_draw_get(EcsWorld* world, const VfxDrawManagerComp* drawManager, const VfxDrawType type) {
  const EcsEntityId drawEntity = vfx_draw_entity(drawManager, type);
  return ecs_utils_write_t(world, DecalDrawView, drawEntity, RendDrawComp);
}

static void vfx_decal_draw_output(
    RendDrawComp*               draw,
    const VfxDecalInstanceComp* instance,
    const GeoVector             pos,
    const GeoQuat               rot,
    const f32                   scale,
    const f32                   alpha) {
  const GeoVector size   = geo_vector_mul_comps(instance->size, geo_vector(scale, scale, 1));
  const GeoBox    box    = geo_box_from_center(pos, size);
  const GeoBox    bounds = geo_box_from_rotated(&box, rot);

  VfxDecalData* out = rend_draw_add_instance_t(draw, VfxDecalData, SceneTags_Vfx, bounds);
  mem_cpy(array_mem(out->data1), mem_create(pos.comps, sizeof(f32) * 3));
  out->data1[3] = (f32)instance->flags;

  geo_quat_pack_f16(rot, out->data2);

  out->data3[0] = float_f32_to_f16(size.x);
  out->data3[1] = float_f32_to_f16(size.y);
  out->data3[2] = float_f32_to_f16(size.z);

  diag_assert_msg(instance->atlasColorIndex <= 1024, "Index not representable by 16 bit float");
  diag_assert_msg(instance->atlasNormalIndex <= 1024, "Index not representable by 16 bit float");

  out->data4[0] = float_f32_to_f16((f32)instance->atlasColorIndex);
  out->data4[1] = float_f32_to_f16((f32)instance->atlasNormalIndex);
  out->data4[2] = float_f32_to_f16(instance->roughness);
  out->data4[3] = float_f32_to_f16(instance->alpha * alpha);
}

ecs_system_define(VfxDecalUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  const VfxAtlasManagerComp* atlasManager = ecs_view_read_t(globalItr, VfxAtlasManagerComp);
  const AssetAtlasComp*      atlasColor   = vfx_atlas(world, atlasManager, VfxAtlasType_DecalColor);
  const AssetAtlasComp*      atlasNormal = vfx_atlas(world, atlasManager, VfxAtlasType_DecalNormal);
  if (!atlasColor || !atlasNormal) {
    return; // Atlas hasn't loaded yet.
  }

  const VfxDrawManagerComp* drawManager = ecs_view_read_t(globalItr, VfxDrawManagerComp);

  RendDrawComp* drawNormal = vfx_draw_get(world, drawManager, VfxDrawType_Decal);
  RendDrawComp* drawDebug  = vfx_draw_get(world, drawManager, VfxDrawType_DecalDebug);

  vfx_decal_draw_init(drawNormal, atlasColor, atlasNormal);
  vfx_decal_draw_init(drawDebug, atlasColor, atlasNormal);

  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const SceneTransformComp*   transComp = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*       scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const SceneTagComp*         tagComp   = ecs_view_read_t(itr, SceneTagComp);
    const SceneVfxDecalComp*    decal     = ecs_view_read_t(itr, SceneVfxDecalComp);
    const VfxDecalInstanceComp* instance  = ecs_view_read_t(itr, VfxDecalInstanceComp);

    const GeoVector transPos   = LIKELY(transComp) ? transComp->position : geo_vector(0);
    const GeoQuat   transRot   = LIKELY(transComp) ? transComp->rotation : geo_quat_ident;
    const f32       transScale = scaleComp ? scaleComp->scale : 1.0f;
    const f32       alpha      = decal->alpha;

    GeoQuat rot;
    switch (instance->projectionAxis) {
    case AssetDecalAxis_LocalY:
      rot = geo_quat_mul(transRot, geo_quat_forward_to_up);
      break;
    case AssetDecalAxis_LocalZ:
      rot = transRot;
      break;
    }

    vfx_decal_draw_output(drawNormal, instance, transPos, rot, transScale, alpha);

    if (UNLIKELY(tagComp && tagComp->tags & SceneTags_Selected)) {
      vfx_decal_draw_output(drawDebug, instance, transPos, rot, transScale, alpha);
    }
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
