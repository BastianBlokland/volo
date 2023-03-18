#include "asset_decal.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_float.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

#define decal_default_thickness 0.25f

static DataReg* g_dataReg;
static DataMeta g_dataDecalDefMeta;

typedef struct {
  String colorAtlasEntry;
  String normalAtlasEntry; // Optional, empty if unused.
  f32    roughness;
  f32    width, height;
  f32    thickness;
} DecalDef;

static void decal_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(reg, DecalDef);
    data_reg_field_t(reg, DecalDef, colorAtlasEntry, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, normalAtlasEntry, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, roughness, data_prim_t(f32));
    data_reg_field_t(reg, DecalDef, width, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, height, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, thickness, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    // clang-format on

    g_dataDecalDefMeta = data_meta_t(t_DecalDef);
    g_dataReg          = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetDecalComp);

ecs_view_define(DecalUnloadView) {
  ecs_access_with(AssetDecalComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any decal-asset components for unloaded assets.
 */
ecs_system_define(DecalUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, DecalUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetDecalComp);
  }
}

static void decal_build_def(const DecalDef* def, AssetDecalComp* out) {
  out->colorAtlasEntry  = string_hash(def->colorAtlasEntry);
  out->normalAtlasEntry = def->normalAtlasEntry.size ? string_hash(def->normalAtlasEntry) : 0;
  out->roughness        = def->roughness;
  out->width            = def->width;
  out->height           = def->height;
  out->thickness        = def->thickness > f32_epsilon ? def->thickness : decal_default_thickness;
}

ecs_module_init(asset_decal_module) {
  decal_datareg_init();

  ecs_register_comp(AssetDecalComp);

  ecs_register_view(DecalUnloadView);

  ecs_register_system(DecalUnloadAssetSys, ecs_view_id(DecalUnloadView));
}

void asset_load_dcl(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  DecalDef       decalDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(
      g_dataReg, src->data, g_alloc_heap, g_dataDecalDefMeta, mem_var(decalDef), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  AssetDecalComp* result = ecs_world_add_t(world, entity, AssetDecalComp);
  decal_build_def(&decalDef, result);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load Decal", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  data_destroy(g_dataReg, g_alloc_heap, g_dataDecalDefMeta, mem_var(decalDef));
  asset_repo_source_close(src);
}
