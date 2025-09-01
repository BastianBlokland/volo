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

typedef struct {
  StringHash language;
  StringHash country;
  String     name;
} LocaleDef;

ecs_comp_define(AssetLocaleComp);

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

static void locale_build_def(const LocaleDef* def, AssetLocaleComp* out) {
  out->language = def->language;
  out->country  = def->country;
  out->name     = string_dup(g_allocHeap, def->name);
}

ecs_module_init(asset_locale_module) {
  ecs_register_comp(AssetLocaleComp);

  ecs_register_view(LocaleUnloadView);

  ecs_register_system(LocaleUnloadAssetSys, ecs_view_id(LocaleUnloadView));
}

void asset_data_init_locale(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, LocaleDef);
  data_reg_field_t(g_dataReg, LocaleDef, language, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, LocaleDef, country, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, LocaleDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
  // clang-format on

  g_assetLocaleDefMeta = data_meta_t(t_LocaleDef);
}

void asset_load_locale(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;
  (void)id;

  LocaleDef      def;
  String         errMsg;
  DataReadResult result;
  if (src->format == AssetFormat_LocaleBin) {
    data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetLocaleDefMeta, mem_var(def), &result);
  } else {
    data_read_json(g_dataReg, src->data, g_allocHeap, g_assetLocaleDefMeta, mem_var(def), &result);
  }
  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }

  AssetLocaleComp* loc = ecs_world_add_t(world, entity, AssetLocaleComp);
  locale_build_def(&def, loc);

  if (src->format != AssetFormat_LocaleBin) {
    // TODO: Instead of caching the definition it would be more optimal to cache the result locale.
    asset_cache(world, entity, g_assetLocaleDefMeta, mem_var(def));
  }

  asset_mark_load_success(world, entity);
  goto Cleanup;

Error:
  asset_mark_load_failure(world, entity, id, errMsg, -1 /* errorCode */);

Cleanup:
  data_destroy(g_dataReg, g_allocHeap, g_assetLocaleDefMeta, mem_var(def));
  asset_repo_close(src);
}
