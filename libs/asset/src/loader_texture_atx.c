#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "data.h"
#include "data_registry.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * ArrayTeXture - Creates multi-layer textures by combining textures.
 */

#define atx_max_textures 100
#define atx_max_layers 256

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
  AtxError_NoTextures,
  AtxError_TooManyTextures,
  AtxError_TooManyLayers,
  AtxError_InvalidTexture,
  AtxError_MismatchType,
  AtxError_MismatchChannels,
  AtxError_MismatchSize,

  AtxError_Count,
} AtxError;

static String atx_error_str(const AtxError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Atx does not specify any textures"),
      string_static("Atx specifies more textures then are supported"),
      string_static("Atx specifies more layers then are supported"),
      string_static("Atx specifies an invalid texture"),
      string_static("Atx textures have different types"),
      string_static("Atx textures have different channel counts"),
      string_static("Atx textures have different sizes"),
  };
  ASSERT(array_elems(g_msgs) == AtxError_Count, "Incorrect number of atx-error messages");
  return g_msgs[err];
}

static void atx_generate(
    const AtxDef*            def,
    const AssetTextureComp** textures,
    AssetTextureComp*        outTexture,
    AtxError*                err) {

  const AssetTextureType     type     = textures[0]->type;
  const AssetTextureChannels channels = textures[0]->channels;
  const u32                  width = textures[0]->width, height = textures[0]->height;
  u32                        layers = textures[0]->layers;

  for (usize i = 1; i != def->textures.count; ++i) {
    if (UNLIKELY(textures[i]->type != type)) {
      *err = AtxError_MismatchType;
      return;
    }
    if (UNLIKELY(textures[i]->channels != channels)) {
      *err = AtxError_MismatchChannels;
      return;
    }
    if (UNLIKELY(textures[i]->width != width || textures[i]->height != height)) {
      *err = AtxError_MismatchSize;
      return;
    }
    layers += math_max(1, textures[i]->layers);
  }
  if (UNLIKELY(layers > atx_max_layers)) {
    *err = AtxError_TooManyLayers;
    return;
  }

  const usize pixelDataSize   = asset_texture_pixel_size(textures[0]);
  const usize textureDataSize = width * height * pixelDataSize * layers;
  const Mem   pixelsMem       = alloc_alloc(g_alloc_heap, textureDataSize, pixelDataSize);

  /**
   * Copy all pixel data to the final array texture.
   */
  Mem pixelWriteMem = pixelsMem;
  for (usize i = 0; i != def->textures.count; ++i) {
    const Mem texMem = asset_texture_data(textures[i]);
    mem_cpy(pixelWriteMem, texMem);
    pixelWriteMem = mem_consume(pixelWriteMem, texMem.size);
  }
  diag_assert(!pixelWriteMem.size);

  *outTexture = (AssetTextureComp){
      .type      = type,
      .channels  = channels,
      .pixelsRaw = pixelsMem.ptr,
      .width     = width,
      .height    = height,
      .layers    = layers,
  };
  *err = AtxError_None;
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_write(AssetAtxLoadComp); }
ecs_view_define(TextureView) { ecs_access_write(AssetTextureComp); }

/**
 * Update all active loads.
 */
ecs_system_define(AtxLoadAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView*     loadView   = ecs_world_view_t(world, LoadView);
  EcsIterator* textureItr = ecs_view_itr(ecs_world_view_t(world, TextureView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    AssetAtxLoadComp* load   = ecs_view_write_t(itr, AssetAtxLoadComp);
    AtxError          err;

    /**
     * Start loading all textures.
     */
    if (!load->textures.size) {
      array_ptr_for_t(load->def.textures, String, texName) {
        const EcsEntityId texAsset                     = asset_lookup(world, manager, *texName);
        *dynarray_push_t(&load->textures, EcsEntityId) = texAsset;
        asset_acquire(world, texAsset);
      }
    }

    /**
     * Gather all textures.
     */
    const AssetTextureComp** textures = mem_stack(sizeof(void*) * load->textures.size).ptr;
    for (usize i = 0; i != load->textures.size; ++i) {
      const EcsEntityId texAsset = *dynarray_at_t(&load->textures, i, EcsEntityId);
      if (ecs_world_has_t(world, texAsset, AssetFailedComp)) {
        err = AtxError_InvalidTexture;
        goto Error;
      }
      if (!ecs_world_has_t(world, texAsset, AssetLoadedComp)) {
        goto Next; // Wait for the texture to be loaded.
      }
      if (UNLIKELY(!ecs_view_maybe_jump(textureItr, texAsset))) {
        err = AtxError_InvalidTexture;
        goto Error;
      }
      textures[i] = ecs_view_read_t(textureItr, AssetTextureComp);
    }

    AssetTextureComp texture;
    atx_generate(&load->def, textures, &texture, &err);
    if (UNLIKELY(err)) {
      goto Error;
    }

    *ecs_world_add_t(world, entity, AssetTextureComp) = texture;
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e("Failed to load Atx array-texture", log_param("error", fmt_text(atx_error_str(err))));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    ecs_world_remove_t(world, entity, AssetAtxLoadComp);
    dynarray_for_t(&load->textures, EcsEntityId, texAsset) { asset_release(world, *texAsset); }

  Next:
    continue;
  }
}

ecs_module_init(asset_atx_module) {
  atx_datareg_init();

  ecs_register_comp(AssetAtxLoadComp, .destructor = ecs_destruct_atx_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(TextureView);

  ecs_register_system(
      AtxLoadAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView), ecs_view_id(TextureView));
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
    errMsg = atx_error_str(AtxError_NoTextures);
    goto Error;
  }
  if (UNLIKELY(def.textures.count > atx_max_textures)) {
    errMsg = atx_error_str(AtxError_TooManyTextures);
    goto Error;
  }
  array_ptr_for_t(def.textures, String, texName) {
    if (UNLIKELY(string_is_empty(*texName))) {
      errMsg = atx_error_str(AtxError_InvalidTexture);
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
