#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "rend_draw.h"

#include "draw_internal.h"

ecs_comp_define(VfxDrawManagerComp) { EcsEntityId drawEntities[VfxDrawType_Count]; };

static const String g_vfxDrawGraphics[VfxDrawType_Count] = {
    [VfxDrawType_Decal]              = string_static("graphics/vfx/decal.graphic"),
    [VfxDrawType_DecalDebug]         = string_static("graphics/vfx/decal_debug.graphic"),
    [VfxDrawType_ParticleForward]    = string_static("graphics/vfx/particle_forward.graphic"),
    [VfxDrawType_ParticleDistortion] = string_static("graphics/vfx/particle_distortion.graphic"),
};

// clang-format off
static const RendDrawFlags g_vfxDrawFlags[VfxDrawType_Count] = {
    [VfxDrawType_Decal]              = RendDrawFlags_Decal | RendDrawFlags_Preload,
    [VfxDrawType_DecalDebug]         = RendDrawFlags_SortBackToFront,
    [VfxDrawType_ParticleForward]    = RendDrawFlags_Particle | RendDrawFlags_Preload | RendDrawFlags_SortBackToFront,
    [VfxDrawType_ParticleDistortion] = RendDrawFlags_Particle | RendDrawFlags_Preload | RendDrawFlags_Distortion,
};
// clang-format on

static EcsEntityId
vfx_draw_create(EcsWorld* world, AssetManagerComp* assets, const VfxDrawType type) {
  const EcsEntityId entity = asset_lookup(world, assets, g_vfxDrawGraphics[type]);
  RendDrawComp*     draw   = rend_draw_create(world, entity, g_vfxDrawFlags[type]);
  rend_draw_set_resource(draw, RendDrawResource_Graphic, entity); // Graphic is on the same entity.
  return entity;
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
