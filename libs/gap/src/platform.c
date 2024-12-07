#include "asset_manager.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "gap_register.h"
#include "log_logger.h"

#include "platform_internal.h"

static const String g_gapIconAssets[GapIcon_Count] = {
    [GapIcon_Main] = string_static("icons/window_main.icon"),
    [GapIcon_Tool] = string_static("icons/window_tool.icon"),
};

static const String g_gapCursorAssets[GapCursor_Count] = {
    [GapCursor_Normal]         = string_static("icons/cursor_normal.icon"),
    [GapCursor_Text]           = string_static("icons/cursor_text.icon"),
    [GapCursor_Click]          = string_static("icons/cursor_click.icon"),
    [GapCursor_Select]         = string_static("icons/cursor_select.icon"),
    [GapCursor_SelectAdd]      = string_static("icons/cursor_select-add.icon"),
    [GapCursor_SelectSubtract] = string_static("icons/cursor_select-subtract.icon"),
    [GapCursor_Target]         = string_static("icons/cursor_target.icon"),
    [GapCursor_Resize]         = string_static("icons/cursor_resize.icon"),
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

ecs_view_define(IconView) { ecs_access_read(AssetIconComp); }

static void gap_icon_load_begin(
    EcsWorld* world, AssetManagerComp* assetManager, GapPlatformIcon* icon, const String assetId) {
  icon->iconAsset = asset_lookup(world, assetManager, assetId);
  icon->loading   = true;
  asset_acquire(world, icon->iconAsset);
}

static bool gap_icon_load_update(EcsWorld* world, GapPlatformIcon* icon, EcsIterator* assetItr) {
  bool success = false;
  if (!icon->iconAsset) {
    goto Ret;
  }
  if (!icon->loading) {
    // When the icon asset changes start loading it again.
    if (ecs_world_has_t(world, icon->iconAsset, AssetChangedComp)) {
      icon->loading = true;
      asset_acquire(world, icon->iconAsset);
    }
    goto Ret;
  }
  if (ecs_world_has_t(world, icon->iconAsset, AssetFailedComp)) {
    goto Done;
  }
  if (!ecs_world_has_t(world, icon->iconAsset, AssetLoadedComp)) {
    goto Ret;
  }
  if (UNLIKELY(!ecs_view_maybe_jump(assetItr, icon->iconAsset))) {
    log_e("Cursor icon asset invalid", log_param("entity", ecs_entity_fmt(icon->iconAsset)));
    goto Done;
  }
  success = true;

Done:
  icon->loading = false;
  asset_release(world, icon->iconAsset);

Ret:
  return success;
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

    for (GapIcon i = 0; i != GapIcon_Count; ++i) {
      gap_icon_load_begin(world, assetManager, &platform->icons[i], g_gapIconAssets[i]);
    }
    for (GapCursor c = 0; c != GapCursor_Count; ++c) {
      if (!string_is_empty(g_gapCursorAssets[c])) {
        gap_icon_load_begin(world, assetManager, &platform->cursors[c], g_gapCursorAssets[c]);
      }
    }
  }

  EcsView*     iconView = ecs_world_view_t(world, IconView);
  EcsIterator* iconItr  = ecs_view_itr(iconView);

  for (GapIcon i = 0; i != GapIcon_Count; ++i) {
    if (gap_icon_load_update(world, &platform->icons[i], iconItr)) {
      gap_pal_icon_load(platform->pal, i, ecs_view_read_t(iconItr, AssetIconComp));
      log_d("Window icon loaded", log_param("id", fmt_text(g_gapIconAssets[i])));
    }
  }

  for (GapCursor c = 0; c != GapCursor_Count; ++c) {
    if (gap_icon_load_update(world, &platform->cursors[c], iconItr)) {
      gap_pal_cursor_load(platform->pal, c, ecs_view_read_t(iconItr, AssetIconComp));
      log_d("Cursor icon loaded", log_param("id", fmt_text(g_gapCursorAssets[c])));
    }
  }

  gap_pal_update(platform->pal);
}

ecs_module_init(gap_platform_module) {
  ecs_register_comp(GapPlatformComp, .destructor = ecs_destruct_platform_comp, .destructOrder = 30);

  ecs_register_view(UpdateGlobalView);
  ecs_register_view(IconView);

  EcsSystemFlags sysFlags = 0;
  if (gap_pal_require_thread_affinity()) {
    sysFlags |= EcsSystemFlags_ThreadAffinity;
  }
  ecs_register_system_with_flags(
      GapPlatformUpdateSys, sysFlags, ecs_view_id(UpdateGlobalView), ecs_view_id(IconView));

  ecs_order(GapPlatformUpdateSys, GapOrder_PlatformUpdate);
}
