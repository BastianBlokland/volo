#include "asset_level.h"
#include "core_alloc.h"
#include "core_path.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

DataMeta g_assetLevelDataDef;

ecs_comp_define_public(AssetLevelComp);

static void ecs_destruct_level_comp(void* data) {
  AssetLevelComp* comp  = data;
  AssetLevel*     level = &comp->level;
  data_destroy(g_dataReg, g_allocHeap, g_assetLevelDataDef, mem_create(level, sizeof(AssetLevel)));
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
  ecs_register_comp(AssetLevelComp, .destructor = ecs_destruct_level_comp);

  ecs_register_view(LevelUnloadView);

  ecs_register_system(LevelUnloadAssetSys, ecs_view_id(LevelUnloadView));
}

void asset_data_init_level(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, GeoVector);
  data_reg_field_t(g_dataReg, GeoVector, x, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoVector, y, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoVector, z, data_prim_t(f32), .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, GeoQuat);
  data_reg_field_t(g_dataReg, GeoQuat, x, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoQuat, y, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoQuat, z, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoQuat, w, data_prim_t(f32), .flags = DataFlags_Opt);

  data_reg_enum_t(g_dataReg, AssetLevelFaction);
  data_reg_const_t(g_dataReg, AssetLevelFaction, None);
  data_reg_const_t(g_dataReg, AssetLevelFaction, A);
  data_reg_const_t(g_dataReg, AssetLevelFaction, B);
  data_reg_const_t(g_dataReg, AssetLevelFaction, C);
  data_reg_const_t(g_dataReg, AssetLevelFaction, D);

  data_reg_struct_t(g_dataReg, AssetLevelObject);
  data_reg_field_t(g_dataReg, AssetLevelObject, id, data_prim_t(u32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetLevelObject, prefab, data_prim_t(String), .flags = DataFlags_NotEmpty | DataFlags_Intern);
  data_reg_field_t(g_dataReg, AssetLevelObject, faction, t_AssetLevelFaction, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevelObject, position, t_GeoVector);
  data_reg_field_t(g_dataReg, AssetLevelObject, rotation, t_GeoQuat);
  data_reg_field_t(g_dataReg, AssetLevelObject, scale, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetLevel);
  data_reg_field_t(g_dataReg, AssetLevel, name, data_prim_t(String), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevel, terrainId, data_prim_t(String), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevel, startpoint, t_GeoVector, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevel, objects, t_AssetLevelObject, .container = DataContainer_Array);
  // clang-format on

  g_assetLevelDataDef = data_meta_t(t_AssetLevel);
}

void asset_load_level(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {

  AssetLevel     level;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_allocHeap, g_assetLevelDataDef, mem_var(level), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  ecs_world_add_t(world, entity, AssetLevelComp, .level = level);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e(
      "Failed to load Level", log_param("id", fmt_text(id)), log_param("error", fmt_text(errMsg)));
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

  DynString dataBuffer = dynstring_create(g_allocHeap, 1 * usize_kibibyte);

  const DataWriteJsonOpts jOpts = data_write_json_opts(.numberMaxDecDigits = 4, .compact = true);
  const Mem               levelData = mem_create(level, sizeof(AssetLevel));
  data_write_json(g_dataReg, &dataBuffer, g_assetLevelDataDef, levelData, &jOpts);

  const bool res = asset_save(manager, idWithExtScratch, dynstring_view(&dataBuffer));

  dynstring_destroy(&dataBuffer);
  return res;
}
