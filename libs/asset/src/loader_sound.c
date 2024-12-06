#include "asset_sound.h"
#include "core_alloc.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "import_internal.h"
#include "repo_internal.h"

DataMeta g_assetSoundMeta;

ecs_comp_define_public(AssetSoundComp);

ecs_comp_define(AssetSoundSourceComp) { AssetSource* src; };

static void ecs_destruct_sound_comp(void* data) {
  AssetSoundComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_assetSoundMeta, mem_create(comp, sizeof(AssetSoundComp)));
}

static void ecs_destruct_sound_source_comp(void* data) {
  AssetSoundSourceComp* comp = data;
  asset_repo_source_close(comp->src);
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetSoundComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any sound-asset components for unloaded assets.
 */
ecs_system_define(UnloadSoundAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetSoundComp);
    ecs_utils_maybe_remove_t(world, entity, AssetSoundSourceComp);
  }
}

ecs_module_init(asset_sound_module) {
  ecs_register_comp(AssetSoundComp, .destructor = ecs_destruct_sound_comp);
  ecs_register_comp(AssetSoundSourceComp, .destructor = ecs_destruct_sound_source_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadSoundAssetSys, ecs_view_id(UnloadView));
}

void asset_data_init_sound(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetSoundComp);
  data_reg_field_t(g_dataReg, AssetSoundComp, frameChannels, data_prim_t(u8), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetSoundComp, frameCount, data_prim_t(u32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetSoundComp, frameRate, data_prim_t(u32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetSoundComp, sampleData, data_prim_t(DataMem), .flags = DataFlags_ExternalMemory);
  // clang-format on

  g_assetSoundMeta = data_meta_t(t_AssetSoundComp);
}

void asset_load_sound_bin(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  AssetSoundComp sound;
  DataReadResult result;
  data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetSoundMeta, mem_var(sound), &result);

  if (UNLIKELY(result.error)) {
    log_e(
        "Failed to load binary sound",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)),
        log_param("error-code", fmt_int(result.error)),
        log_param("error", fmt_text(result.errorMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);
    asset_repo_source_close(src);
    return;
  }

  *ecs_world_add_t(world, entity, AssetSoundComp) = sound;
  ecs_world_add_t(world, entity, AssetSoundSourceComp, .src = src);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
}
