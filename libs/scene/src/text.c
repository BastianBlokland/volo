#include "asset_ftx.h"
#include "asset_manager.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_renderable.h"
#include "scene_text.h"

static const String g_textGraphic = string_static("graphics/ui/text.gra");
static const String g_textFont    = string_static("fonts/mono.ftx");

typedef enum {
  SceneGlobalFont_Acquired  = 1 << 0,
  SceneGlobalFont_Unloading = 1 << 1,
} SceneGlobalFontFlags;

ecs_comp_define_public(SceneTextComp);
ecs_comp_define(SceneGlobalFontComp) {
  EcsEntityId          asset;
  SceneGlobalFontFlags flags;
};

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(GlobalFontView) { ecs_access_write(SceneGlobalFontComp); }
ecs_view_define(FtxView) { ecs_access_read(AssetFtxComp); }

ecs_view_define(TextInitView) {
  ecs_access_with(SceneTextComp);
  ecs_access_without(SceneRenderableUniqueComp);
}

ecs_system_define(SceneTextInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);
  if (!ecs_world_has_t(world, ecs_view_entity(globalItr), SceneGlobalFontComp)) {
    const EcsEntityId fontAsset = asset_lookup(world, assets, g_textFont);
    ecs_world_add_t(world, ecs_view_entity(globalItr), SceneGlobalFontComp, .asset = fontAsset);
  }

  EcsView* renderView = ecs_world_view_t(world, TextInitView);
  for (EcsIterator* itr = ecs_view_itr(renderView); ecs_view_walk(itr);) {
    ecs_world_add_t(
        world,
        ecs_view_entity(itr),
        SceneRenderableUniqueComp,
        .graphic = asset_lookup(world, assets, g_textGraphic));
  }
}

ecs_system_define(SceneTextUnloadChangedFontsSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalFontView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneGlobalFontComp* globalFonts = ecs_view_write_t(globalItr, SceneGlobalFontComp);
  const bool           isLoaded    = ecs_world_has_t(world, globalFonts->asset, AssetLoadedComp);
  const bool           hasChanged  = ecs_world_has_t(world, globalFonts->asset, AssetChangedComp);

  if (globalFonts->flags & SceneGlobalFont_Acquired && isLoaded && hasChanged) {
    log_i("Unloading global font due to changed asset", log_param("id", fmt_text(g_textFont)));
    asset_release(world, globalFonts->asset);
    globalFonts->flags &= ~SceneGlobalFont_Acquired;
    globalFonts->flags |= SceneGlobalFont_Unloading;
  }
  if (globalFonts->flags & SceneGlobalFont_Unloading && !isLoaded) {
    globalFonts->flags &= ~SceneGlobalFont_Unloading;
  }
}

ecs_view_define(TextRenderView) {
  ecs_access_read(SceneTextComp);
  ecs_access_write(SceneRenderableUniqueComp);
}

ecs_system_define(SceneTextRenderSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalFontView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneGlobalFontComp* globalFonts = ecs_view_write_t(globalItr, SceneGlobalFontComp);
  if (!(globalFonts->flags & (SceneGlobalFont_Acquired | SceneGlobalFont_Unloading))) {
    asset_acquire(world, globalFonts->asset);
    globalFonts->flags |= SceneGlobalFont_Acquired;
  }
  EcsView* ftxView = ecs_world_view_t(world, FtxView);
  if (!ecs_view_contains(ftxView, globalFonts->asset)) {
    return; // Ftx font is not loaded.
  }
  const AssetFtxComp* ftx = ecs_utils_read_t(world, FtxView, globalFonts->asset, AssetFtxComp);
  (void)ftx;

  EcsView* renderView = ecs_world_view_t(world, TextRenderView);
  for (EcsIterator* itr = ecs_view_itr(renderView); ecs_view_walk(itr);) {
    const SceneTextComp*       textComp   = ecs_view_read_t(itr, SceneTextComp);
    SceneRenderableUniqueComp* renderable = ecs_view_write_t(itr, SceneRenderableUniqueComp);

    (void)textComp;
    renderable->vertexCountOverride = 6;
  }
}

ecs_module_init(scene_text_module) {
  ecs_register_comp(SceneTextComp);
  ecs_register_comp(SceneGlobalFontComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GlobalFontView);
  ecs_register_view(FtxView);
  ecs_register_view(TextInitView);
  ecs_register_view(TextRenderView);

  ecs_register_system(SceneTextInitSys, ecs_view_id(GlobalAssetsView), ecs_view_id(TextInitView));
  ecs_register_system(SceneTextUnloadChangedFontsSys, ecs_view_id(GlobalFontView));
  ecs_register_system(
      SceneTextRenderSys,
      ecs_view_id(GlobalFontView),
      ecs_view_id(FtxView),
      ecs_view_id(TextRenderView));
}
