#include "asset/level.h"
#include "asset/property.h"
#include "core/alloc.h"
#include "core/dynstring.h"
#include "core/float.h"
#include "core/path.h"
#include "core/search.h"
#include "core/sort.h"
#include "data/read.h"
#include "data/utils.h"
#include "data/write.h"
#include "ecs/entity.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "log/logger.h"

#include "data.h"
#include "manager.h"
#include "repo.h"

DataMeta g_assetLevelDefMeta;

ecs_comp_define_public(AssetLevelComp);

static void ecs_destruct_level_comp(void* data) {
  AssetLevelComp* comp  = data;
  AssetLevel*     level = &comp->level;
  data_destroy(g_dataReg, g_allocHeap, g_assetLevelDefMeta, mem_create(level, sizeof(AssetLevel)));
}

static i8 level_compare_object_id(const void* a, const void* b) {
  return compare_u32(field_ptr(a, AssetLevelObject, id), field_ptr(b, AssetLevelObject, id));
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
  data_reg_enum_t(g_dataReg, AssetLevelFaction);
  data_reg_const_t(g_dataReg, AssetLevelFaction, None);
  data_reg_const_t(g_dataReg, AssetLevelFaction, A);
  data_reg_const_t(g_dataReg, AssetLevelFaction, B);
  data_reg_const_t(g_dataReg, AssetLevelFaction, C);
  data_reg_const_t(g_dataReg, AssetLevelFaction, D);

  data_reg_enum_t(g_dataReg, AssetLevelFog);
  data_reg_const_t(g_dataReg, AssetLevelFog, Disabled);
  data_reg_const_t(g_dataReg, AssetLevelFog, VisibilityBased);

  data_reg_struct_t(g_dataReg, AssetLevelObject);
  data_reg_field_t(g_dataReg, AssetLevelObject, id, data_prim_t(u32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetLevelObject, prefab, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetLevelObject, faction, t_AssetLevelFaction, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevelObject, position, g_assetGeoVec3Type, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevelObject, rotation, g_assetGeoQuatType, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevelObject, scale, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetLevelObject, properties, g_assetPropertyType, .container = DataContainer_HeapArray, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevelObject, sets, data_prim_t(StringHash), .container = DataContainer_InlineArray, .fixedCount = asset_level_sets_max, .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetLevel);
  data_reg_field_t(g_dataReg, AssetLevel, name, data_prim_t(String), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevel, terrain, g_assetRefType, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevel, fogMode, t_AssetLevelFog, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevel, startpoint, g_assetGeoVec3Type, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLevel, objects, t_AssetLevelObject, .container = DataContainer_HeapArray);
  // clang-format on

  g_assetLevelDefMeta = data_meta_t(t_AssetLevel);
}

void asset_load_level(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  AssetLevel     lvl;
  String         errMsg;
  DataReadResult readRes;
  if (src->format == AssetFormat_LevelBin) {
    data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetLevelDefMeta, mem_var(lvl), &readRes);
  } else {
    data_read_json(g_dataReg, src->data, g_allocHeap, g_assetLevelDefMeta, mem_var(lvl), &readRes);

    /**
     * Ensure the objects are sorted on their id. The editor always produces json files with sorted
     * objects but external edits (for example source control merges) can cause non-sorted files.
     */
    sort_quicksort_t(
        lvl.objects.values,
        lvl.objects.values + lvl.objects.count,
        AssetLevelObject,
        level_compare_object_id);
  }
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  ecs_world_add_t(world, entity, AssetLevelComp, .level = lvl);
  asset_mark_load_success(world, entity);

  if (src->format != AssetFormat_LevelBin) {
    asset_cache(world, entity, g_assetLevelDefMeta, mem_var(lvl));
  }

  goto Cleanup;

Error:
  asset_mark_load_failure(world, entity, id, errMsg, -1 /* errorCode */);

Cleanup:
  asset_repo_close(src);
}

u32 asset_level_refs(
    const AssetLevelComp* comp,
    EcsWorld*             world,
    AssetManagerComp*     assets,
    EcsEntityId           out[],
    const u32             outMax) {
  const Mem levelMem = mem_var(comp->level);
  return asset_data_query_refs_unpatched(world, assets, g_assetLevelDefMeta, levelMem, out, outMax);
}

const AssetLevelObject* asset_level_find(const AssetLevel* lvl, const u32 persistentId) {
  return search_binary_t(
      lvl->objects.values,
      lvl->objects.values + lvl->objects.count,
      AssetLevelObject,
      level_compare_object_id,
      &(AssetLevelObject){.id = persistentId});
}

u32 asset_level_find_index(const AssetLevel* lvl, const u32 persistentId) {
  const AssetLevelObject* obj = asset_level_find(lvl, persistentId);
  if (!obj) {
    return sentinel_u32;
  }
  return (u32)(obj - lvl->objects.values);
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

  DynString dataBuffer = dynstring_create(g_allocHeap, 512 * usize_kibibyte);

  const DataWriteJsonOpts jOpts = data_write_json_opts(
          .numberMaxDecDigits    = 4,
          .numberExpThresholdPos = f64_max, // Disable positive scientific notation.
          .numberExpThresholdNeg = 0,       // Disable negative scientific notation.
          .compact               = true);

  const Mem levelData = mem_create(level, sizeof(AssetLevel));
  data_write_json(g_dataReg, &dataBuffer, g_assetLevelDefMeta, levelData, &jOpts);
  dynstring_append_char(&dataBuffer, '\n'); // End the file with a new-line.

  const bool res = asset_save(manager, idWithExtScratch, dynstring_view(&dataBuffer));

  dynstring_destroy(&dataBuffer);
  return res;
}
