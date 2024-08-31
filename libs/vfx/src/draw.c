#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "rend_object.h"

#include "draw_internal.h"

ecs_comp_define(VfxDrawManagerComp) { EcsEntityId drawEntities[VfxDrawType_Count]; };

// clang-format off

// NOTE: Single and Trail decals are split so both can be filled in parallel.
static const String g_vfxDrawGraphics[VfxDrawType_Count] = {
    [VfxDrawType_DecalStampSingle]         = string_static("graphics/vfx/stamp.graphic"),
    [VfxDrawType_DecalStampSingleDebug]    = string_static("graphics/vfx/stamp_debug.graphic"),
    [VfxDrawType_DecalStampTrail]          = string_static("graphics/vfx/stamp.graphic"),
    [VfxDrawType_DecalStampTrailDebug]     = string_static("graphics/vfx/stamp_debug.graphic"),
    [VfxDrawType_ParticleSpriteForward]    = string_static("graphics/vfx/sprite_forward.graphic"),
    [VfxDrawType_ParticleSpriteDistortion] = string_static("graphics/vfx/sprite_distortion.graphic"),
};

static const RendObjectFlags g_vfxDrawFlags[VfxDrawType_Count] = {
    [VfxDrawType_DecalStampSingle]         = RendObjectFlags_Decal | RendObjectFlags_Preload,
    [VfxDrawType_DecalStampSingleDebug]    = RendObjectFlags_SortBackToFront,
    [VfxDrawType_DecalStampTrail]          = RendObjectFlags_Decal | RendObjectFlags_Preload,
    [VfxDrawType_DecalStampTrailDebug]     = RendObjectFlags_SortBackToFront,
    [VfxDrawType_ParticleSpriteForward]    = RendObjectFlags_VfxSprite | RendObjectFlags_Preload | RendObjectFlags_SortBackToFront,
    [VfxDrawType_ParticleSpriteDistortion] = RendObjectFlags_VfxSprite | RendObjectFlags_Preload | RendObjectFlags_Distortion,
};
// clang-format on

static EcsEntityId
vfx_draw_create(EcsWorld* world, AssetManagerComp* assets, const VfxDrawType type) {
  const EcsEntityId drawEntity  = ecs_world_entity_create(world);
  const EcsEntityId assetEntity = asset_lookup(world, assets, g_vfxDrawGraphics[type]);

  RendDrawComp* draw = rend_draw_create(world, drawEntity, g_vfxDrawFlags[type]);
  rend_draw_set_resource(draw, RendDrawResource_Graphic, assetEntity);
  return drawEntity;
}

ecs_view_define(InitGlobalView) {
  ecs_access_without(VfxDrawManagerComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(VfxDrawManagerInitSys) {
  EcsView* initView = ecs_world_view_t(world, InitGlobalView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    AssetManagerComp* assets = ecs_view_write_t(itr, AssetManagerComp);

    VfxDrawManagerComp* manager = ecs_world_add_t(world, entity, VfxDrawManagerComp);
    for (VfxDrawType type = 0; type != VfxDrawType_Count; ++type) {
      manager->drawEntities[type] = vfx_draw_create(world, assets, type);
    }
  }
}

ecs_module_init(vfx_draw_module) {
  ecs_register_comp(VfxDrawManagerComp);

  ecs_register_system(VfxDrawManagerInitSys, ecs_register_view(InitGlobalView));
}

EcsEntityId vfx_draw_entity(const VfxDrawManagerComp* manager, const VfxDrawType type) {
  return manager->drawEntities[type];
}
