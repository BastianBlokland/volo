#include "asset_terrain.h"
#include "core_alloc.h"
#include "core_thread.h"
#include "data.h"
#include "data_schema.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

#define terrain_max_size 1500
#define terrain_max_height 50.0f

static DataReg* g_dataReg;
static DataMeta g_dataMeta;

static void terrain_datareg_init(void) {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(reg, AssetTerrainColor);
    data_reg_field_t(reg, AssetTerrainColor, r, data_prim_t(f32));
    data_reg_field_t(reg, AssetTerrainColor, g, data_prim_t(f32));
    data_reg_field_t(reg, AssetTerrainColor, b, data_prim_t(f32));
    data_reg_comment_t(reg, AssetTerrainColor, "Srgb encoded color value");

    data_reg_struct_t(reg, AssetTerrainComp);
    data_reg_field_t(reg, AssetTerrainComp, graphicId, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetTerrainComp, heightmapId, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetTerrainComp, size, data_prim_t(u32));
    data_reg_field_t(reg, AssetTerrainComp, playSize, data_prim_t(u32));
    data_reg_field_t(reg, AssetTerrainComp, heightMax, data_prim_t(f32));
    data_reg_field_t(reg, AssetTerrainComp, minimapColorLow, t_AssetTerrainColor, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetTerrainComp, minimapColorHigh, t_AssetTerrainColor, .flags = DataFlags_Opt);
    // clang-format on

    g_dataMeta = data_meta_t(t_AssetTerrainComp);
    g_dataReg  = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetTerrainComp);
ecs_comp_define(AssetTerrainLoadComp) { AssetSource* src; };

static void ecs_destruct_terrain_comp(void* data) {
  AssetTerrainComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataMeta, mem_create(comp, sizeof(AssetTerrainComp)));
}

static void ecs_destruct_terrain_load_comp(void* data) {
  AssetTerrainLoadComp* comp = data;
  asset_repo_source_close(comp->src);
}

static void terrain_load_fail(EcsWorld* world, const EcsEntityId entity, const String msg) {
  log_e("Failed to parse terrain", log_param("error", fmt_text(msg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_read(AssetTerrainLoadComp); }

ecs_view_define(UnloadView) {
  ecs_access_read(AssetTerrainComp);
  ecs_access_without(AssetLoadedComp);
}

static bool terrain_color_validate(const AssetTerrainColor* color) {
  if (color->r < 0.0f || color->r > 1.0f) {
    return false;
  }
  if (color->g < 0.0f || color->g > 1.0f) {
    return false;
  }
  if (color->b < 0.0f || color->b > 1.0f) {
    return false;
  }
  return true;
}

/**
 * Load terrain-assets.
 */
ecs_system_define(LoadTerrainAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity      = ecs_view_entity(itr);
    const AssetSource* src         = ecs_view_read_t(itr, AssetTerrainLoadComp)->src;
    AssetTerrainComp*  terrainComp = ecs_world_add_t(world, entity, AssetTerrainComp);

    DataReadResult result;
    data_read_json(
        g_dataReg,
        src->data,
        g_alloc_heap,
        g_dataMeta,
        mem_create(terrainComp, sizeof(AssetTerrainComp)),
        &result);
    if (result.error) {
      terrain_load_fail(world, entity, result.errorMsg);
      goto Error;
    }

    if (!terrainComp->size || terrainComp->size > terrain_max_size) {
      terrain_load_fail(world, entity, string_lit("Invalid terrain size"));
      goto Error;
    }
    if (!terrainComp->playSize || terrainComp->playSize > terrainComp->size) {
      terrain_load_fail(world, entity, string_lit("Invalid terrain play size"));
      goto Error;
    }
    if (terrainComp->playSize % 2) {
      terrain_load_fail(world, entity, string_lit("Terrain play size has to be divisible by two"));
      goto Error;
    }
    if (terrainComp->heightMax < 0.0f || terrainComp->heightMax > terrain_max_height) {
      terrain_load_fail(world, entity, string_lit("Invalid terrain maximum height"));
      goto Error;
    }
    if (!terrain_color_validate(&terrainComp->minimapColorLow)) {
      terrain_load_fail(world, entity, string_lit("Invalid minimap color low"));
      goto Error;
    }
    if (!terrain_color_validate(&terrainComp->minimapColorHigh)) {
      terrain_load_fail(world, entity, string_lit("Invalid minimap color high"));
      goto Error;
    }

    // Resolve asset references.
    terrainComp->graphic   = asset_lookup(world, manager, terrainComp->graphicId);
    terrainComp->heightmap = asset_lookup(world, manager, terrainComp->heightmapId);
    asset_register_dep(world, entity, terrainComp->graphic);
    asset_register_dep(world, entity, terrainComp->heightmap);

    ecs_world_remove_t(world, entity, AssetTerrainLoadComp);
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    continue;

  Error:
    // NOTE: 'AssetTerrainComp' will be cleaned up by 'UnloadTerrainAssetSys'.
    ecs_world_remove_t(world, entity, AssetTerrainLoadComp);
  }
}

/**
 * Remove any terrain-asset components for unloaded assets.
 */
ecs_system_define(UnloadTerrainAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetTerrainComp);
  }
}

ecs_module_init(asset_terrain_module) {
  terrain_datareg_init();

  ecs_register_comp(AssetTerrainComp, .destructor = ecs_destruct_terrain_comp);
  ecs_register_comp(AssetTerrainLoadComp, .destructor = ecs_destruct_terrain_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadTerrainAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadTerrainAssetSys, ecs_view_id(UnloadView));
}

void asset_load_terrain(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetTerrainLoadComp, .src = src);
}

void asset_terrain_jsonschema_write(DynString* str) {
  terrain_datareg_init();

  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_dataMeta, schemaFlags);
}
