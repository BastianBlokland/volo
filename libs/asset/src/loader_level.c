#include "asset_level.h"
#include "core_alloc.h"
#include "core_path.h"
#include "core_thread.h"
#include "data.h"
#include "data_schema.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataLevelMeta;

static void level_datareg_init(void) {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(reg, GeoVector);
    data_reg_field_t(reg, GeoVector, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, GeoVector, y, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, GeoVector, z, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_struct_t(reg, GeoQuat);
    data_reg_field_t(reg, GeoQuat, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, GeoQuat, y, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, GeoQuat, z, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, GeoQuat, w, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_enum_t(reg, AssetLevelFaction);
    data_reg_const_t(reg, AssetLevelFaction, None);
    data_reg_const_t(reg, AssetLevelFaction, A);
    data_reg_const_t(reg, AssetLevelFaction, B);
    data_reg_const_t(reg, AssetLevelFaction, C);
    data_reg_const_t(reg, AssetLevelFaction, D);

    data_reg_struct_t(reg, AssetLevelObject);
    data_reg_field_t(reg, AssetLevelObject, id, data_prim_t(u32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetLevelObject, prefab, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetLevelObject, faction, t_AssetLevelFaction, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetLevelObject, position, t_GeoVector);
    data_reg_field_t(reg, AssetLevelObject, rotation, t_GeoQuat);
    data_reg_field_t(reg, AssetLevelObject, scale, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetLevel);
    data_reg_field_t(reg, AssetLevel, name, data_prim_t(String), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetLevel, terrainId, data_prim_t(String), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetLevel, startpoint, t_GeoVector, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetLevel, objects, t_AssetLevelObject, .container = DataContainer_Array);
    // clang-format on

    g_dataLevelMeta = data_meta_t(t_AssetLevel);
    g_dataReg       = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetLevelComp);

static void ecs_destruct_level_comp(void* data) {
  AssetLevelComp* comp  = data;
  AssetLevel*     level = &comp->level;
  data_destroy(g_dataReg, g_alloc_heap, g_dataLevelMeta, mem_create(level, sizeof(AssetLevel)));
}

ecs_view_define(LevelUnloadView) {
  ecs_access_with(AssetLevelComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any level-asset component for unloaded assets.
 */
ecs_system_define(LevelUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, LevelUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetLevelComp);
  }
}

ecs_module_init(asset_level_module) {
  level_datareg_init();

  ecs_register_comp(AssetLevelComp, .destructor = ecs_destruct_level_comp);

  ecs_register_view(LevelUnloadView);

  ecs_register_system(LevelUnloadAssetSys, ecs_view_id(LevelUnloadView));
}

void asset_load_level(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  AssetLevel     level;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataLevelMeta, mem_var(level), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  ecs_world_add_t(world, entity, AssetLevelComp, .level = level);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load Level", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
}

bool asset_level_save(AssetManagerComp* manager, const String id, const AssetLevel* level) {
  String       idWithExtScratch = id;
  const String ext              = path_extension(id);
  if (string_is_empty(ext)) {
    idWithExtScratch = fmt_write_scratch("{}.level", fmt_text(id));
  } else if (!string_eq(ext, string_lit("level"))) {
    log_w(
        "Level cannot be saved",
        log_param("id", fmt_text(id)),
        log_param("reason", fmt_text_lit("Invalid extension")));
    return false;
  }

  DynString dataBuffer = dynstring_create(g_alloc_heap, 1 * usize_kibibyte);

  const DataWriteJsonOpts jOpts = data_write_json_opts(.numberMaxDecDigits = 4, .compact = true);
  const Mem               levelData = mem_create(level, sizeof(AssetLevel));
  data_write_json(g_dataReg, &dataBuffer, g_dataLevelMeta, levelData, &jOpts);

  const bool res = asset_save(manager, idWithExtScratch, dynstring_view(&dataBuffer));

  dynstring_destroy(&dataBuffer);
  return res;
}

void asset_level_jsonschema_write(DynString* str) {
  level_datareg_init();

  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_dataLevelMeta, schemaFlags);
}
