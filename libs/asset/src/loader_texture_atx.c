#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "data.h"
#include "data_registry.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * ArrayTeXture - Creates multi-layer textures by combining textures.
 */

#define atx_max_layers 100

static DataReg* g_dataReg;
static DataMeta g_dataAtxDefMeta;

typedef struct {
  struct {
    String* values;
    usize   count;
  } textures;
} AtxDef;

static void atx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, AtxDef);
    data_reg_field_t(g_dataReg, AtxDef, textures, data_prim_t(String), .flags = DataFlags_NotEmpty, .container = DataContainer_Array);
    // clang-format on

    g_dataAtxDefMeta = data_meta_t(t_AtxDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define(AssetAtxLoadComp) {
  AtxDef   def;
  DynArray textures; // EcsEntityId[].
};

static void ecs_destruct_atx_load_comp(void* data) {
  AssetAtxLoadComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataAtxDefMeta, mem_var(comp->def));
}

typedef enum {
  AtxError_None = 0,
  AtxError_NoLayers,
  AtxError_TooManyLayers,
  AtxError_EmptyTextureIdentifier,

  AtxError_Count,
} AtxError;

static String atx_error_str(const AtxError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Atx does not specify any layers"),
      string_static("Atx specifies more layers then are supported"),
      string_static("Atx specifies an empty texture identifier"),
  };
  ASSERT(array_elems(g_msgs) == AtxError_Count, "Incorrect number of atx-error messages");
  return g_msgs[err];
}

ecs_module_init(asset_atx_module) {
  atx_datareg_init();

  ecs_register_comp(AssetAtxLoadComp, .destructor = ecs_destruct_atx_load_comp);
}

void asset_load_atx(EcsWorld* world, const EcsEntityId entity, AssetSource* src) {

  String         errMsg;
  AtxDef         def;
  DataReadResult result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataAtxDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(!def.textures.count)) {
    errMsg = atx_error_str(AtxError_NoLayers);
    goto Error;
  }
  if (UNLIKELY(def.textures.count > atx_max_layers)) {
    errMsg = atx_error_str(AtxError_TooManyLayers);
    goto Error;
  }
  array_ptr_for_t(def.textures, String, texName) {
    if (UNLIKELY(string_is_empty(*texName))) {
      errMsg = atx_error_str(AtxError_EmptyTextureIdentifier);
      goto Error;
    }
  }

  ecs_world_add_t(
      world,
      entity,
      AssetAtxLoadComp,
      .def      = def,
      .textures = dynarray_create_t(g_alloc_heap, EcsEntityId, def.textures.count));
  asset_repo_source_close(src);
  return;

Error:
  log_e("Failed to load atx texture", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  data_destroy(g_dataReg, g_alloc_heap, g_dataAtxDefMeta, mem_var(def));
  asset_repo_source_close(src);
}
