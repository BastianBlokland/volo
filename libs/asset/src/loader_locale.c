#include "asset/locale.h"
#include "core/alloc.h"
#include "data/read.h"
#include "data/utils.h"
#include "ecs/entity.h"
#include "ecs/view.h"
#include "ecs/world.h"

#include "manager.h"
#include "repo.h"

DataMeta g_assetLocaleDefMeta;

ecs_comp_define(AssetLocaleComp);

static void ecs_destruct_locale_comp(void* data) {
  AssetLocaleComp* comp = data;
  data_destroy(
      g_dataReg, g_allocHeap, g_assetLocaleDefMeta, mem_create(comp, sizeof(AssetLocaleComp)));
}

static i8 locale_text_compare(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, AssetLocaleText, key), field_ptr(b, AssetLocaleText, key));
}

ecs_view_define(LocaleUnloadView) {
  ecs_access_with(AssetLocaleComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any locale-asset components for unloaded assets.
 */
ecs_system_define(LocaleUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, LocaleUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetLocaleComp);
  }
}

ecs_module_init(asset_locale_module) {
  ecs_register_comp(AssetLocaleComp, .destructor = ecs_destruct_locale_comp);

  ecs_register_system(LocaleUnloadAssetSys, ecs_register_view(LocaleUnloadView));
}

void asset_data_init_locale(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetLocaleText);
  data_reg_field_t(g_dataReg, AssetLocaleText, key, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetLocaleText, value, data_prim_t(String));
  data_reg_compare_t(g_dataReg, AssetLocaleText, locale_text_compare);
  data_reg_comment_t(g_dataReg, AssetLocaleText, "Translation key / value.");

  data_reg_struct_t(g_dataReg, AssetLocaleComp);
  data_reg_field_t(g_dataReg, AssetLocaleComp, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetLocaleComp, isDefault, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetLocaleComp, textEntries, t_AssetLocaleText, .container = DataContainer_HeapArray, .flags = DataFlags_Sort);
  // clang-format on

  g_assetLocaleDefMeta = data_meta_t(t_AssetLocaleComp);
}

void asset_load_locale(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;
  (void)id;

  AssetLocaleComp* localeComp = ecs_world_add_t(world, entity, AssetLocaleComp);
  const Mem        localeMem  = mem_create(localeComp, sizeof(AssetLocaleComp));

  DataReadResult result;
  if (src->format == AssetFormat_LocaleBin) {
    data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetLocaleDefMeta, localeMem, &result);
  } else {
    data_read_json(g_dataReg, src->data, g_allocHeap, g_assetLocaleDefMeta, localeMem, &result);
  }
  if (result.error) {
    asset_mark_load_failure(world, entity, id, result.errorMsg, -1 /* errorCode */);
    goto Ret;
    // NOTE: 'AssetLocaleComp' will be cleaned up by 'LocaleUnloadAssetSys'.
  }

  if (src->format != AssetFormat_LocaleBin) {
    asset_cache(world, entity, g_assetLocaleDefMeta, localeMem);
  }

  asset_mark_load_success(world, entity);

Ret:
  asset_repo_close(src);
}
