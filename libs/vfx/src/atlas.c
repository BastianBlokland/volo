#include "asset_atlas.h"
#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "atlas_internal.h"

static const String g_vfxAtlasAssets[VfxAtlasType_Count] = {
    [VfxAtlasType_Sprite]      = string_static("textures/vfx/sprite.atlas"),
    [VfxAtlasType_StampColor]  = string_static("textures/vfx/stamp_color.atlas"),
    [VfxAtlasType_StampNormal] = string_static("textures/vfx/stamp_normal.atlas"),
};

static const String g_vfxAtlasTypeNames[] = {
    string_static("sprite"),
    string_static("stamp-color"),
    string_static("stamp-normal"),
};
ASSERT(array_elems(g_vfxAtlasTypeNames) == VfxAtlasType_Count, "Incorrect number of names");

typedef enum {
  VfxAtlas_Acquired  = 1 << 0,
  VfxAtlas_Unloading = 1 << 1,
} VfxAtlasFlags;

typedef struct {
  VfxAtlasFlags flags;
  EcsEntityId   entity;
} VfxAtlasData;

ecs_comp_define(VfxAtlasManagerComp) { VfxAtlasData atlases[VfxAtlasType_Count]; };

ecs_view_define(InitGlobalView) {
  ecs_access_maybe_write(VfxAtlasManagerComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(VfxAtlasInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, InitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (UNLIKELY(!globalItr)) {
    return;
  }
  AssetManagerComp*    assets  = ecs_view_write_t(globalItr, AssetManagerComp);
  VfxAtlasManagerComp* manager = ecs_view_write_t(globalItr, VfxAtlasManagerComp);

  if (UNLIKELY(!manager)) {
    manager = ecs_world_add_t(world, ecs_world_global(world), VfxAtlasManagerComp);
    for (VfxAtlasType type = 0; type != VfxAtlasType_Count; ++type) {
      manager->atlases[type].entity = asset_lookup(world, assets, g_vfxAtlasAssets[type]);
    }
  }

  for (VfxAtlasType type = 0; type != VfxAtlasType_Count; ++type) {
    VfxAtlasData* atlas = &manager->atlases[type];
    if (!(atlas->flags & (VfxAtlas_Acquired | VfxAtlas_Unloading))) {
      log_i(
          "Acquiring vfx {} atlas",
          log_param("type", fmt_text(g_vfxAtlasTypeNames[type])),
          log_param("id", fmt_text(g_vfxAtlasAssets[type])));
      asset_acquire(world, atlas->entity);
      atlas->flags |= VfxAtlas_Acquired;
    }
  }
}

ecs_view_define(UnloadChangedGlobalView) { ecs_access_write(VfxAtlasManagerComp); }

ecs_system_define(VfxAtlasUnloadChangedSys) {
  EcsView*     globalView = ecs_world_view_t(world, UnloadChangedGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (UNLIKELY(!globalItr)) {
    return;
  }
  VfxAtlasManagerComp* manager = ecs_view_write_t(globalItr, VfxAtlasManagerComp);

  for (VfxAtlasType type = 0; type != VfxAtlasType_Count; ++type) {
    VfxAtlasData* atlas      = &manager->atlases[type];
    const bool    isLoaded   = ecs_world_has_t(world, atlas->entity, AssetLoadedComp);
    const bool    isFailed   = ecs_world_has_t(world, atlas->entity, AssetFailedComp);
    const bool    hasChanged = ecs_world_has_t(world, atlas->entity, AssetChangedComp);

    if (atlas->flags & VfxAtlas_Acquired && (isLoaded || isFailed) && hasChanged) {
      log_i(
          "Unloading vfx {} atlas",
          log_param("type", fmt_text(g_vfxAtlasTypeNames[type])),
          log_param("id", fmt_text(g_vfxAtlasAssets[type])),
          log_param("reason", fmt_text_lit("Asset changed")));

      asset_release(world, atlas->entity);
      atlas->flags &= ~VfxAtlas_Acquired;
      atlas->flags |= VfxAtlas_Unloading;
    }
    if (atlas->flags & VfxAtlas_Unloading && !isLoaded) {
      atlas->flags &= ~VfxAtlas_Unloading;
    }
  }
}

ecs_module_init(vfx_atlas_module) {
  ecs_register_comp(VfxAtlasManagerComp);

  ecs_register_system(VfxAtlasInitSys, ecs_register_view(InitGlobalView));
  ecs_register_system(VfxAtlasUnloadChangedSys, ecs_register_view(UnloadChangedGlobalView));
}

EcsEntityId vfx_atlas_entity(const VfxAtlasManagerComp* manager, const VfxAtlasType type) {
  diag_assert(type < VfxAtlasType_Count);
  return manager->atlases[type].entity;
}

VfxAtlasDrawData vfx_atlas_draw_data(const AssetAtlasComp* atlas) {
  const f32 atlasEntrySize             = 1.0f / atlas->entriesPerDim;
  const f32 atlasEntrySizeMinusPadding = atlasEntrySize - atlas->entryPadding * 2;

  return (VfxAtlasDrawData){
      .atlasEntriesPerDim         = atlas->entriesPerDim,
      .atlasEntrySize             = atlasEntrySize,
      .atlasEntrySizeMinusPadding = atlasEntrySizeMinusPadding,
      .atlasEntryPadding          = atlas->entryPadding,
  };
}
