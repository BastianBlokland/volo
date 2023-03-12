#include "asset_atlas.h"
#include "asset_manager.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_draw.h"

// clang-format off
static const String         g_vfxDecalGraphic    = string_static("graphics/vfx/decal.gra");
static const String         g_vfxDecalAtlasColor = string_static("textures/vfx/decal_color.atl");
static const RendDrawFlags  g_vfxDecalDrawFlags  = /* RendDrawFlags_Decal | */ RendDrawFlags_Preload;
// clang-format on

typedef enum {
  VfxRenderer_AtlasAcquired  = 1 << 0,
  VfxRenderer_AtlasUnloading = 1 << 1,
} VfxRendererFlags;

ecs_comp_define(VfxDecalRendererComp) {
  VfxRendererFlags flags;
  EcsEntityId      atlasColor;
  EcsEntityId      drawEntity;
};

ecs_comp_define(VfxDecalDrawComp);

static EcsEntityId vfx_decal_draw_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId entity = asset_lookup(world, assets, g_vfxDecalGraphic);
  ecs_world_add_empty_t(world, entity, VfxDecalDrawComp);
  RendDrawComp* draw = rend_draw_create(world, entity, g_vfxDecalDrawFlags);
  rend_draw_set_graphic(draw, entity); // Graphic is on the same entity as the draw.
  return entity;
}

ecs_view_define(GlobalView) {
  ecs_access_maybe_write(VfxDecalRendererComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(VfxDecalRendererInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp*     assets   = ecs_view_write_t(globalItr, AssetManagerComp);
  VfxDecalRendererComp* renderer = ecs_view_write_t(globalItr, VfxDecalRendererComp);

  if (!renderer) {
    renderer             = ecs_world_add_t(world, ecs_world_global(world), VfxDecalRendererComp);
    renderer->atlasColor = asset_lookup(world, assets, g_vfxDecalAtlasColor);
    renderer->drawEntity = vfx_decal_draw_create(world, assets);
  }

  if (!(renderer->flags & (VfxRenderer_AtlasAcquired | VfxRenderer_AtlasUnloading))) {
    log_i("Acquiring decal atlas", log_param("id", fmt_text(g_vfxDecalAtlasColor)));
    asset_acquire(world, renderer->atlasColor);
    renderer->flags |= VfxRenderer_AtlasAcquired;
  }
}

ecs_system_define(VfxDecalUnloadChangedAtlasSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  VfxDecalRendererComp* renderer = ecs_view_write_t(globalItr, VfxDecalRendererComp);
  if (renderer) {
    const bool isLoaded   = ecs_world_has_t(world, renderer->atlasColor, AssetLoadedComp);
    const bool isFailed   = ecs_world_has_t(world, renderer->atlasColor, AssetFailedComp);
    const bool hasChanged = ecs_world_has_t(world, renderer->atlasColor, AssetChangedComp);

    if (renderer->flags & VfxRenderer_AtlasAcquired && (isLoaded || isFailed) && hasChanged) {
      log_i(
          "Unloading decal atlas",
          log_param("id", fmt_text(g_vfxDecalAtlasColor)),
          log_param("reason", fmt_text_lit("Asset changed")));

      asset_release(world, renderer->atlasColor);
      renderer->flags &= ~VfxRenderer_AtlasAcquired;
      renderer->flags |= VfxRenderer_AtlasUnloading;
    }
    if (renderer->flags & VfxRenderer_AtlasUnloading && !isLoaded) {
      renderer->flags &= ~VfxRenderer_AtlasUnloading;
    }
  }
}

ecs_module_init(vfx_decal_module) {
  ecs_register_comp(VfxDecalRendererComp);
  ecs_register_comp_empty(VfxDecalDrawComp);

  ecs_register_view(GlobalView);

  ecs_register_system(VfxDecalRendererInitSys, ecs_view_id(GlobalView));
  ecs_register_system(VfxDecalUnloadChangedAtlasSys, ecs_view_id(GlobalView));
}
