#include "asset_manager.h"
#include "ecs_world.h"
#include "gap_register.h"

#include "platform_internal.h"

static const String g_gapCursorAssets[GapCursor_Count] = {
    [GapCursor_Normal] = string_static("cursors/normal.cursor"),
};

ecs_comp_define_public(GapPlatformComp);

static void ecs_destruct_platform_comp(void* data) {
  GapPlatformComp* comp = data;
  gap_pal_destroy(comp->pal);
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_maybe_write(GapPlatformComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(GapPlatformUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized yet.
  }
  AssetManagerComp* assetManager = ecs_view_write_t(globalItr, AssetManagerComp);
  GapPlatformComp*  platform     = ecs_view_write_t(globalItr, GapPlatformComp);
  if (!platform) {
    platform      = ecs_world_add_t(world, ecs_world_global(world), GapPlatformComp);
    platform->pal = gap_pal_create(g_allocHeap);

    for (GapCursor c = 0; c != GapCursor_Count; ++c) {
      if (string_is_empty(g_gapCursorAssets[c])) {
        continue;
      }
      platform->cursors[c].asset = asset_lookup(world, assetManager, g_gapCursorAssets[c]);
      asset_acquire(world, platform->cursors[c].asset);
    }
  }

  gap_pal_update(platform->pal);
}

ecs_module_init(gap_platform_module) {
  ecs_register_comp(GapPlatformComp, .destructor = ecs_destruct_platform_comp, .destructOrder = 30);

  ecs_register_view(UpdateGlobalView);

  EcsSystemFlags sysFlags = 0;
  if (gap_pal_require_thread_affinity()) {
    sysFlags |= EcsSystemFlags_ThreadAffinity;
  }
  ecs_register_system_with_flags(GapPlatformUpdateSys, sysFlags, ecs_view_id(UpdateGlobalView));
  ecs_order(GapPlatformUpdateSys, GapOrder_PlatformUpdate);
}
