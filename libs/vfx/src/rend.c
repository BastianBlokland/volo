#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "rend_object.h"

#include "rend_internal.h"

ecs_comp_define(VfxRendComp) { EcsEntityId rendObjects[VfxRendObj_Count]; };

// clang-format off

// NOTE: Single and Trail decals are split so both can be filled in parallel.
static const String g_vfxObjGraphics[VfxRendObj_Count] = {
  [VfxRendObj_DecalStampSingle]         = string_static("graphics/vfx/stamp.graphic"),
  [VfxRendObj_DecalStampSingleDebug]    = string_static("graphics/vfx/stamp_debug.graphic"),
  [VfxRendObj_DecalStampTrail]          = string_static("graphics/vfx/stamp.graphic"),
  [VfxRendObj_DecalStampTrailDebug]     = string_static("graphics/vfx/stamp_debug.graphic"),
  [VfxRendObj_ParticleSpriteForward]    = string_static("graphics/vfx/sprite_forward.graphic"),
  [VfxRendObj_ParticleSpriteDistortion] = string_static("graphics/vfx/sprite_distortion.graphic"),
};

static const String g_vfxObjGraphicsWireframe[VfxRendObj_Count] = {
  [VfxRendObj_DecalStampSingle]         = string_static("graphics/vfx/stamp_wireframe.graphic"),
  [VfxRendObj_DecalStampTrail]          = string_static("graphics/vfx/stamp_wireframe.graphic"),
  [VfxRendObj_ParticleSpriteForward]    = string_static("graphics/vfx/sprite_wireframe.graphic"),
  [VfxRendObj_ParticleSpriteDistortion] = string_static("graphics/vfx/sprite_wireframe.graphic"),
};

static const String g_vfxObjGraphicsShadow[VfxRendObj_Count] = {
  [VfxRendObj_ParticleSpriteForward]    = string_static("graphics/vfx/sprite_shadow.graphic"),
};

static const RendObjectFlags g_vfxObjFlags[VfxRendObj_Count] = {
  [VfxRendObj_DecalStampSingle]         = RendObjectFlags_Preload,
  [VfxRendObj_DecalStampSingleDebug]    = RendObjectFlags_SortBackToFront,
  [VfxRendObj_DecalStampTrail]          = RendObjectFlags_Preload,
  [VfxRendObj_DecalStampTrailDebug]     = RendObjectFlags_SortBackToFront,
  [VfxRendObj_ParticleSpriteForward]    = RendObjectFlags_Preload | RendObjectFlags_SortBackToFront,
  [VfxRendObj_ParticleSpriteDistortion] = RendObjectFlags_Preload,
};
// clang-format on

static EcsEntityId
vfx_rend_obj_create(EcsWorld* world, AssetManagerComp* assets, const VfxRendObj type) {
  const EcsEntityId objEntity = ecs_world_entity_create(world);
  RendObjectComp*   rendObj   = rend_object_create(world, objEntity, g_vfxObjFlags[type]);

  rend_object_set_resource(
      rendObj, RendObjectRes_Graphic, asset_lookup(world, assets, g_vfxObjGraphics[type]));

  if (!string_is_empty(g_vfxObjGraphicsShadow[type])) {
    rend_object_set_resource(
        rendObj,
        RendObjectRes_GraphicShadow,
        asset_lookup(world, assets, g_vfxObjGraphicsShadow[type]));
  }

  if (!string_is_empty(g_vfxObjGraphicsWireframe[type])) {
    rend_object_set_resource(
        rendObj,
        RendObjectRes_GraphicDebugWireframe,
        asset_lookup(world, assets, g_vfxObjGraphicsWireframe[type]));
  }

  return objEntity;
}

ecs_view_define(InitGlobalView) {
  ecs_access_without(VfxRendComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(VfxRendInitSys) {
  EcsView* initView = ecs_world_view_t(world, InitGlobalView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    AssetManagerComp* assets = ecs_view_write_t(itr, AssetManagerComp);

    VfxRendComp* manager = ecs_world_add_t(world, entity, VfxRendComp);
    for (VfxRendObj type = 0; type != VfxRendObj_Count; ++type) {
      manager->rendObjects[type] = vfx_rend_obj_create(world, assets, type);
    }
  }
}

ecs_module_init(vfx_rend_module) {
  ecs_register_comp(VfxRendComp);

  ecs_register_system(VfxRendInitSys, ecs_register_view(InitGlobalView));
}

EcsEntityId vfx_rend_obj(const VfxRendComp* manager, const VfxRendObj type) {
  return manager->rendObjects[type];
}
