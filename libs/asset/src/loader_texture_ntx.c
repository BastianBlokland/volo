#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * NoiseTeXture - Procedurally generated noise textures.
 */

#define ntx_max_size (1024 * 16)

static DataReg* g_dataReg;
static DataMeta g_dataNtxDefMeta;

typedef struct {
  u32 size;
} NtxDef;

static void ntx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, NtxDef);
    data_reg_field_t(g_dataReg, NtxDef, size, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    // clang-format on

    g_dataNtxDefMeta = data_meta_t(t_NtxDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef enum {
  NtxError_None = 0,
  NtxError_SizeNonPow2,
  NtxError_SizeTooBig,

  NtxError_Count,
} NtxError;

static String ntx_error_str(const NtxError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Ntx specifies a non power-of-two texture size"),
      string_static("Ntx specifies a texture size larger then is supported"),
  };
  ASSERT(array_elems(g_msgs) == NtxError_Count, "Incorrect number of ntx-error messages");
  return g_msgs[err];
}

static void ntx_generate(const NtxDef* def, AssetTextureComp* outTexture, NtxError* err) {
  const u32           size   = def->size;
  AssetTexturePixel1* pixels = alloc_array_t(g_alloc_heap, AssetTexturePixel1, size * size);
  mem_set(mem_create(pixels, sizeof(AssetTexturePixel1) * size * size), 128);

  *outTexture = (AssetTextureComp){
      .channels = AssetTextureChannels_One,
      .pixels1  = pixels,
      .width    = size,
      .height   = size,
  };
  *err = NtxError_None;
}

ecs_module_init(asset_ntx_module) { ntx_datareg_init(); }

void asset_load_ntx(EcsWorld* world, const EcsEntityId entity, AssetSource* src) {
  String         errMsg;
  NtxDef         def;
  DataReadResult result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataNtxDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(!bits_ispow2(def.size))) {
    errMsg = ntx_error_str(NtxError_SizeNonPow2);
    goto Error;
  }
  if (UNLIKELY(def.size > ntx_max_size)) {
    errMsg = ntx_error_str(NtxError_SizeTooBig);
    goto Error;
  }

  NtxError         err;
  AssetTextureComp texture;
  ntx_generate(&def, &texture, &err);
  if (UNLIKELY(err)) {
    goto Error;
  }
  *ecs_world_add_t(world, entity, AssetTextureComp) = texture;
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  asset_repo_source_close(src);
  return;

Error:
  log_e("Failed to load ntx noise-texture", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  data_destroy(g_dataReg, g_alloc_heap, g_dataNtxDefMeta, mem_var(def));
  asset_repo_source_close(src);
}
