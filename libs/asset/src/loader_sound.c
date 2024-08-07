#include "asset_sound.h"
#include "core_alloc.h"
#include "ecs_world.h"

#include "repo_internal.h"

ecs_comp_define_public(AssetSoundComp);

static void ecs_destruct_sound_comp(void* data) {
  AssetSoundComp* comp = data;
  alloc_free_array_t(g_allocHeap, comp->samples, comp->frameCount * comp->frameChannels);
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetSoundComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any sound-asset components for unloaded assets.
 */
ecs_system_define(UnloadSoundAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetSoundComp);
  }
}

ecs_module_init(asset_sound_module) {
  ecs_register_comp(AssetSoundComp, .destructor = ecs_destruct_sound_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadSoundAssetSys, ecs_view_id(UnloadView));
}
