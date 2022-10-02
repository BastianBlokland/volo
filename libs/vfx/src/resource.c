#include "asset_manager.h"
#include "ecs_world.h"

#include "resource_internal.h"

static const String g_vfxParticleGraphic = string_static("graphics/vfx/particle.gra");

ecs_comp_define(VfxGlobalResourcesComp) { EcsEntityId particleGraphic; };

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(GlobalResourcesView) { ecs_access_write(VfxGlobalResourcesComp); }

static AssetManagerComp* vfx_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static VfxGlobalResourcesComp* vfx_global_resources(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourcesView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, VfxGlobalResourcesComp) : null;
}

ecs_system_define(VfxResourceInitSys) {
  AssetManagerComp* assets = vfx_asset_manager(world);
  if (!assets) {
    return; // Asset manager hasn't been initialized yet.
  }

  VfxGlobalResourcesComp* globalResources = vfx_global_resources(world);
  if (!globalResources) {
    ecs_world_add_t(
        world,
        ecs_world_global(world),
        VfxGlobalResourcesComp,
        .particleGraphic = asset_lookup(world, assets, g_vfxParticleGraphic));
  }
}

ecs_module_init(vfx_resource_module) {
  ecs_register_comp(VfxGlobalResourcesComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GlobalResourcesView);

  ecs_register_system(
      VfxResourceInitSys, ecs_view_id(GlobalAssetsView), ecs_view_id(GlobalResourcesView));
}

EcsEntityId vfx_resource_particle_graphic(const VfxGlobalResourcesComp* comp) {
  return comp->particleGraphic;
}
