#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "data.h"
#include "data_registry.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

/**
 * ATLas - Creates atlas textures by combining other textures.
 */

#define atlas_max_entries 256

static DataReg* g_dataReg;
static DataMeta g_dataAtlasDefMeta;

typedef struct {
  String name;
  String texture;
} AtlasEntryDef;

typedef struct {
  bool mipmaps;
  struct {
    AtlasEntryDef* values;
    usize          count;
  } entries;
} AtlasDef;

static void atlas_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, AtlasEntryDef);
    data_reg_field_t(g_dataReg, AtlasEntryDef, name, data_prim_t(String));
    data_reg_field_t(g_dataReg, AtlasEntryDef, texture, data_prim_t(String));

    data_reg_struct_t(g_dataReg, AtlasDef);
    data_reg_field_t(g_dataReg, AtlasDef, mipmaps, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AtlasDef, entries, t_AtlasEntryDef, .flags = DataFlags_NotEmpty, .container = DataContainer_Array);
    // clang-format on

    g_dataAtlasDefMeta = data_meta_t(t_AtlasDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define(AssetAtlasLoadComp) {
  AtlasDef def;
  DynArray textures; // EcsEntityId[].
};

static void ecs_destruct_atlas_load_comp(void* data) {
  AssetAtlasLoadComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataAtlasDefMeta, mem_var(comp->def));
  dynarray_destroy(&comp->textures);
}

typedef enum {
  AtlasError_None = 0,
  AtlasError_NoEntries,
  AtlasError_TooManyEntries,
  AtlasError_InvalidTexture,

  AtlasError_Count,
} AtlasError;

static String atlas_error_str(const AtlasError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Atlas does not specify any entries"),
      string_static("Atlas specifies more entries then are supported"),
      string_static("Atlas specifies an invalid texture"),
  };
  ASSERT(array_elems(g_msgs) == AtlasError_Count, "Incorrect number of atlas-error messages");
  return g_msgs[err];
}

static void atlas_generate(
    const AtlasDef*          def,
    const AssetTextureComp** textures,
    AssetTextureComp*        outTexture,
    AtlasError*              err) {

  (void)def;
  (void)textures;

  *outTexture = (AssetTextureComp){0};
  *err        = AtlasError_None;
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_write(AssetAtlasLoadComp); }
ecs_view_define(TextureView) { ecs_access_read(AssetTextureComp); }

/**
 * Update all active loads.
 */
ecs_system_define(AtlasLoadAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView*     loadView   = ecs_world_view_t(world, LoadView);
  EcsIterator* textureItr = ecs_view_itr(ecs_world_view_t(world, TextureView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId   entity = ecs_view_entity(itr);
    AssetAtlasLoadComp* load   = ecs_view_write_t(itr, AssetAtlasLoadComp);
    AtlasError          err;

    /**
     * Start loading all entry textures.
     */
    if (!load->textures.size) {
      array_ptr_for_t(load->def.entries, AtlasEntryDef, entryDef) {
        const EcsEntityId texAsset = asset_lookup(world, manager, entryDef->texture);
        *dynarray_push_t(&load->textures, EcsEntityId) = texAsset;
        asset_acquire(world, texAsset);
        asset_register_dep(world, entity, texAsset);
      }
    }

    /**
     * Gather all textures.
     */
    const AssetTextureComp** textures = mem_stack(sizeof(void*) * load->textures.size).ptr;
    for (usize i = 0; i != load->textures.size; ++i) {
      const EcsEntityId texAsset = *dynarray_at_t(&load->textures, i, EcsEntityId);
      if (ecs_world_has_t(world, texAsset, AssetFailedComp)) {
        err = AtlasError_InvalidTexture;
        goto Error;
      }
      if (!ecs_world_has_t(world, texAsset, AssetLoadedComp)) {
        goto Next; // Wait for the texture to be loaded.
      }
      if (UNLIKELY(!ecs_view_maybe_jump(textureItr, texAsset))) {
        err = AtlasError_InvalidTexture;
        goto Error;
      }
      textures[i] = ecs_view_read_t(textureItr, AssetTextureComp);
    }

    AssetTextureComp texture;
    atlas_generate(&load->def, textures, &texture, &err);
    if (UNLIKELY(err)) {
      goto Error;
    }

    *ecs_world_add_t(world, entity, AssetTextureComp) = texture;
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e("Failed to load atlas texture", log_param("error", fmt_text(atlas_error_str(err))));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    ecs_world_remove_t(world, entity, AssetAtlasLoadComp);
    dynarray_for_t(&load->textures, EcsEntityId, texAsset) { asset_release(world, *texAsset); }

  Next:
    continue;
  }
}

ecs_module_init(asset_atlas_module) {
  atlas_datareg_init();

  ecs_register_comp(AssetAtlasLoadComp, .destructor = ecs_destruct_atlas_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(TextureView);

  ecs_register_system(
      AtlasLoadAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView), ecs_view_id(TextureView));
}

void asset_load_atl(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  String         errMsg;
  AtlasDef       def;
  DataReadResult result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataAtlasDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(!def.entries.count)) {
    errMsg = atlas_error_str(AtlasError_NoEntries);
    goto Error;
  }
  if (UNLIKELY(def.entries.count > atlas_max_entries)) {
    errMsg = atlas_error_str(AtlasError_TooManyEntries);
    goto Error;
  }
  array_ptr_for_t(def.entries, String, texName) {
    if (UNLIKELY(string_is_empty(*texName))) {
      errMsg = atlas_error_str(AtlasError_InvalidTexture);
      goto Error;
    }
  }

  ecs_world_add_t(
      world,
      entity,
      AssetAtlasLoadComp,
      .def      = def,
      .textures = dynarray_create_t(g_alloc_heap, EcsEntityId, def.entries.count));
  asset_repo_source_close(src);
  return;

Error:
  log_e("Failed to load atlas texture", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  data_destroy(g_dataReg, g_alloc_heap, g_dataAtlasDefMeta, mem_var(def));
  asset_repo_source_close(src);
}
