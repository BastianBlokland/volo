#include "asset_font.h"
#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_math.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataFtxDefMeta;

typedef struct {
  String fontId;
  u32    size;
  u32    border;
  u32    character;
} FtxDefinition;

static void ftx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    data_reg_struct_t(g_dataReg, FtxDefinition);
    data_reg_field_t(g_dataReg, FtxDefinition, fontId, data_prim_t(String));
    data_reg_field_t(g_dataReg, FtxDefinition, size, data_prim_t(u32));
    data_reg_field_t(g_dataReg, FtxDefinition, border, data_prim_t(u32));
    data_reg_field_t(g_dataReg, FtxDefinition, character, data_prim_t(u32));

    g_dataFtxDefMeta = data_meta_t(t_FtxDefinition);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define(AssetFtxLoadComp) {
  FtxDefinition def;
  EcsEntityId   font;
};

static void ecs_destruct_ftx_load_comp(void* data) {
  AssetFtxLoadComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataFtxDefMeta, mem_var(comp->def));
}

typedef enum {
  FtxError_None = 0,
  FtxError_FontNotSpecified,
  FtxError_FontInvalid,
  FtxError_NonPow2Size,

  FtxError_Count,
} FtxError;

static String ftx_error_str(const FtxError err) {
  static const String msgs[] = {
      string_static("None"),
      string_static("Ftx definition does not specify a font"),
      string_static("Ftx definition specifies an invalid font"),
      string_static("Ftx definition specifies a non power-of-two texture size"),
  };
  ASSERT(array_elems(msgs) == FtxError_Count, "Incorrect number of ftx-error messages");
  return msgs[err];
}

static void ftx_generate_sdf(
    const FtxDefinition* def, const AssetFontComp* font, AssetTexturePixel* out, FtxError* err) {

  const AssetFontGlyph* glyph   = asset_font_lookup(font, def->character);
  const u32             size    = def->size;
  const f32             invSize = 1.0f / def->size;
  const f32             offset  = def->border * invSize;
  const f32             scale   = 1.0f + offset * 2.0f;

  for (usize pixelY = 0; pixelY != size; ++pixelY) {
    for (usize pixelX = 0; pixelX != size; ++pixelX) {
      const AssetFontPoint point = {
          .x = ((pixelX + 0.5f) * invSize - offset) * scale,
          .y = ((pixelY + 0.5f) * invSize - offset) * scale,
      };
      const f32 dist                = asset_font_glyph_dist(font, glyph, point);
      const f32 borderFrac          = math_clamp_f32(dist / offset, -1, 1);
      out[pixelY * size + pixelX].a = (u8)((-borderFrac * 0.5f + 0.5f) * 255.999f);
    }
  }

  *err = FtxError_None;
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_write(AssetFtxLoadComp); }
ecs_view_define(FontView) { ecs_access_write(AssetFontComp); }

/**
 * Update all active loads.
 */
ecs_system_define(FtxLoadAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* loadView = ecs_world_view_t(world, LoadView);
  EcsView* fontView = ecs_world_view_t(world, FontView);

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    AssetFtxLoadComp* load   = ecs_view_write_t(itr, AssetFtxLoadComp);
    const u32         size   = load->def.size;
    FtxError          err;

    if (!load->font) {
      load->font = asset_lookup(world, manager, load->def.fontId);
      asset_acquire(world, load->font);
    }
    if (ecs_world_has_t(world, load->font, AssetFailedComp)) {
      err = FtxError_FontInvalid;
      goto Error;
    }
    if (!ecs_world_has_t(world, load->font, AssetLoadedComp)) {
      continue; // Wait for the font to load.
    }
    EcsIterator* fontItr = ecs_view_maybe_at(fontView, load->font);
    if (UNLIKELY(!fontItr)) {
      err = FtxError_FontInvalid;
      goto Error;
    }
    const AssetFontComp* font = ecs_view_read_t(fontItr, AssetFontComp);

    const Mem pixelMem = alloc_alloc(g_alloc_heap, size * size * sizeof(AssetTexturePixel), 1);
    mem_set(pixelMem, 0);

    ftx_generate_sdf(&load->def, font, pixelMem.ptr, &err);
    if (UNLIKELY(err)) {
      alloc_free(g_alloc_heap, pixelMem);
      goto Error;
    }

    ecs_world_add_t(
        world, entity, AssetTextureComp, .width = size, .height = size, .pixels = pixelMem.ptr);
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e("Failed to load Ftx font-texture", log_param("error", fmt_text(ftx_error_str(err))));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    ecs_world_remove_t(world, entity, AssetFtxLoadComp);
    if (load->font) {
      asset_release(world, load->font);
    }
  }
}

ecs_module_init(asset_ftx_module) {
  ftx_datareg_init();

  ecs_register_comp(AssetFtxLoadComp, .destructor = ecs_destruct_ftx_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(FontView);

  ecs_register_system(
      FtxLoadAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView), ecs_view_id(FontView));
}

void asset_load_ftx(EcsWorld* world, const EcsEntityId entity, AssetSource* src) {
  String         errMsg;
  FtxDefinition  def;
  DataReadResult result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataFtxDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(string_is_empty(def.fontId))) {
    errMsg = ftx_error_str(FtxError_FontNotSpecified);
    goto Error;
  }
  if (UNLIKELY(!bits_ispow2(def.size))) {
    errMsg = ftx_error_str(FtxError_NonPow2Size);
    goto Error;
  }

  ecs_world_add_t(world, entity, AssetFtxLoadComp, .def = def);
  goto Cleanup;

Error:
  log_e("Failed to load Ftx font-texture", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
}
