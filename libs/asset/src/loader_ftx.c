#include "asset_font.h"
#include "asset_ftx.h"
#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_search.h"
#include "core_sort.h"
#include "core_thread.h"
#include "core_utf8.h"
#include "data.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * FontTeXture - Generates a sdf texture atlas and a character mapping based on a font file.
 */

#define ftx_max_chars 1024
#define ftx_max_size (1024 * 16)

static DataReg* g_dataReg;
static DataMeta g_dataFtxDefMeta;

typedef struct {
  String fontId;
  u32    size, glyphSize;
  u32    border;
  f32    lineSpacing;
  String characters;
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
    data_reg_field_t(g_dataReg, FtxDefinition, glyphSize, data_prim_t(u32));
    data_reg_field_t(g_dataReg, FtxDefinition, border, data_prim_t(u32));
    data_reg_field_t(
        g_dataReg, FtxDefinition, lineSpacing, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, FtxDefinition, characters, data_prim_t(String));

    g_dataFtxDefMeta = data_meta_t(t_FtxDefinition);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetFtxComp);

ecs_comp_define(AssetFtxLoadComp) {
  FtxDefinition def;
  EcsEntityId   font;
};

static void ecs_destruct_ftx_comp(void* data) {
  AssetFtxComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->characters, comp->characterCount);
}

static void ecs_destruct_ftx_load_comp(void* data) {
  AssetFtxLoadComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataFtxDefMeta, mem_var(comp->def));
}

typedef enum {
  FtxError_None = 0,
  FtxError_FontNotSpecified,
  FtxError_FontInvalid,
  FtxError_FontGlyphMissing,
  FtxError_SizeNonPow2,
  FtxError_SizeZero,
  FtxError_SizeTooBig,
  FtxError_GlyphSizeNonPow2,
  FtxError_NoCharacters,
  FtxError_TooManyCharacters,
  FtxError_TooManyGlyphs,
  FtxError_InvalidUtf8,

  FtxError_Count,
} FtxError;

static String ftx_error_str(const FtxError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Ftx does not specify a font"),
      string_static("Ftx specifies an invalid font"),
      string_static("Ftx source font is missing a glyph for the requested characters"),
      string_static("Ftx specifies a non power-of-two texture size"),
      string_static("Ftx specifies a zero texture size"),
      string_static("Ftx specifies a texture size larger then is supported"),
      string_static("Ftx specifies a non power-of-two glyph size"),
      string_static("Ftx does not specify any characters"),
      string_static("Ftx specifies more characters then are supported"),
      string_static("Ftx requires more glyphs then fit at the requested size"),
      string_static("Ftx specifies invalid utf8"),
  };
  ASSERT(array_elems(g_msgs) == FtxError_Count, "Incorrect number of ftx-error messages");
  return g_msgs[err];
}

static i8 ftx_compare_char_cp(const void* a, const void* b) {
  return compare_u32(field_ptr(a, AssetFtxChar, cp), field_ptr(b, AssetFtxChar, cp));
}

typedef struct {
  u32                   cp;
  const AssetFontGlyph* glyph;
} FtxDefinitionChar;

static u32 ftx_lookup_chars(
    const AssetFontComp* font, String chars, FtxDefinitionChar out[ftx_max_chars], FtxError* err) {

  u32 index    = 0;
  out[index++] = (FtxDefinitionChar){.cp = 0, .glyph = asset_font_missing(font)};
  do {
    Unicode cp;
    chars = utf8_cp_read(chars, &cp);
    if (UNLIKELY(index >= ftx_max_chars)) {
      *err = FtxError_TooManyCharacters;
      return 0;
    }
    if (UNLIKELY(!cp)) {
      *err = FtxError_InvalidUtf8;
      return 0;
    }
    const AssetFontGlyph* glyph = asset_font_lookup(font, cp);
    if (UNLIKELY(glyph == asset_font_missing(font))) {
      *err = FtxError_FontGlyphMissing;
      return 0;
    }
    out[index++] = (FtxDefinitionChar){.cp = cp, .glyph = glyph};

  } while (chars.size);

  *err = FtxError_None;
  return index;
}

static void ftx_generate_glyph(
    const FtxDefinition*  def,
    const AssetFontComp*  font,
    const AssetFontGlyph* glyph,
    const u32             index,
    AssetTexturePixel*    out) {

  const u32 texY = index * def->glyphSize / def->size * def->glyphSize;
  const u32 texX = index * def->glyphSize % def->size;

  diag_assert(texY + def->glyphSize <= def->size);
  diag_assert(texX + def->glyphSize <= def->size);

  const u32 glyphSize    = def->glyphSize;
  const f32 invGlyphSize = 1.0f / glyphSize;
  const f32 offset       = def->border * invGlyphSize;
  const f32 scale        = 1.0f + offset * 2.0f;

  for (usize glyphPixelY = 0; glyphPixelY != glyphSize; ++glyphPixelY) {
    for (usize glyphPixelX = 0; glyphPixelX != glyphSize; ++glyphPixelX) {
      const AssetFontPoint point = {
          .x = ((glyphPixelX + 0.5f) * invGlyphSize - offset) * scale,
          .y = ((glyphPixelY + 0.5f) * invGlyphSize - offset) * scale,
      };
      const f32 dist       = asset_font_glyph_dist(font, glyph, point);
      const f32 borderFrac = math_clamp_f32(dist / offset, -1, 1);
      const u8  alpha      = (u8)((-borderFrac * 0.5f + 0.5f) * 255.999f);

      const usize texPixelY                  = texY + glyphPixelY;
      const usize texPixelX                  = texX + glyphPixelX;
      out[texPixelY * def->size + texPixelX] = (AssetTexturePixel){0, 0, 0, alpha};
    }
  }
}

static void ftx_generate(
    const FtxDefinition* def,
    const AssetFontComp* font,
    AssetFtxComp*        outFtx,
    AssetTextureComp*    outTexture,
    FtxError*            err) {

  AssetFtxChar*      chars        = null;
  AssetTexturePixel* pixels       = null;
  const u32          size         = def->size;
  const u32          glyphsPerDim = size / def->glyphSize;
  const u32          maxGlyphs    = glyphsPerDim * glyphsPerDim;
  if (UNLIKELY(!maxGlyphs)) {
    *err = FtxError_TooManyGlyphs;
    goto Error;
  }

  FtxDefinitionChar inputChars[ftx_max_chars];
  const u32         charCount = ftx_lookup_chars(font, def->characters, inputChars, err);
  if (UNLIKELY(*err)) {
    goto Error;
  }
  chars  = alloc_array_t(g_alloc_heap, AssetFtxChar, charCount);
  pixels = alloc_array_t(g_alloc_heap, AssetTexturePixel, size * size);

  u32 nextGlyphIndex = 0;
  for (u32 i = 0; i != charCount; ++i) {
    /**
     * Take the sdf border into account as the glyph will need to be rendered bigger to compensate.
     */
    const f32 relGlyphBorder = def->border / (f32)def->glyphSize * inputChars[i].glyph->size;

    chars[i] = (AssetFtxChar){
        .cp         = inputChars[i].cp,
        .glyphIndex = inputChars[i].glyph->segmentCount ? nextGlyphIndex : sentinel_u32,
        .size       = inputChars[i].glyph->size + relGlyphBorder * 2.0f,
        .offsetX    = inputChars[i].glyph->offsetX - relGlyphBorder,
        .offsetY    = inputChars[i].glyph->offsetY - relGlyphBorder,
        .advance    = inputChars[i].glyph->advance,
    };
    if (inputChars[i].glyph->segmentCount) {
      if (UNLIKELY(nextGlyphIndex >= maxGlyphs)) {
        *err = FtxError_TooManyGlyphs;
        goto Error;
      }
      ftx_generate_glyph(def, font, inputChars[i].glyph, nextGlyphIndex++, pixels);
    }
  }

  // Sort the characters on the unicode codepoint.
  sort_quicksort_t(chars, chars + charCount, AssetFtxChar, ftx_compare_char_cp);

  *outFtx = (AssetFtxComp){
      .glyphsPerDim   = glyphsPerDim,
      .lineSpacing    = def->lineSpacing,
      .characters     = chars,
      .characterCount = charCount,
  };
  *outTexture = (AssetTextureComp){
      .pixels = pixels,
      .width  = size,
      .height = size,
  };
  *err = FtxError_None;
  return;

Error:
  diag_assert(*err);
  if (chars) {
    alloc_free_array_t(g_alloc_heap, chars, charCount);
  }
  if (pixels) {
    alloc_free_array_t(g_alloc_heap, pixels, size * size);
  }
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

    AssetFtxComp     ftx;
    AssetTextureComp texture;
    ftx_generate(&load->def, font, &ftx, &texture, &err);
    if (UNLIKELY(err)) {
      goto Error;
    }

    *ecs_world_add_t(world, entity, AssetFtxComp)     = ftx;
    *ecs_world_add_t(world, entity, AssetTextureComp) = texture;
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

ecs_view_define(FtxUnloadView) {
  ecs_access_with(AssetFtxComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any ftx-asset component for unloaded assets.
 */
ecs_system_define(FtxUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, FtxUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetFtxComp);
  }
}

ecs_module_init(asset_ftx_module) {
  ftx_datareg_init();

  ecs_register_comp(AssetFtxComp, .destructor = ecs_destruct_ftx_comp);
  ecs_register_comp(AssetFtxLoadComp, .destructor = ecs_destruct_ftx_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(FontView);
  ecs_register_view(FtxUnloadView);

  ecs_register_system(
      FtxLoadAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView), ecs_view_id(FontView));

  ecs_register_system(FtxUnloadAssetSys, ecs_view_id(FtxUnloadView));
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
  if (UNLIKELY(!def.size || !def.glyphSize)) {
    errMsg = ftx_error_str(FtxError_SizeZero);
    goto Error;
  }
  if (UNLIKELY(!bits_ispow2(def.size))) {
    errMsg = ftx_error_str(FtxError_SizeNonPow2);
    goto Error;
  }
  if (UNLIKELY(def.size > ftx_max_size)) {
    errMsg = ftx_error_str(FtxError_SizeTooBig);
    goto Error;
  }
  if (UNLIKELY(!bits_ispow2(def.glyphSize))) {
    errMsg = ftx_error_str(FtxError_GlyphSizeNonPow2);
    goto Error;
  }
  if (UNLIKELY(string_is_empty(def.characters))) {
    errMsg = ftx_error_str(FtxError_NoCharacters);
    goto Error;
  }

  ecs_world_add_t(world, entity, AssetFtxLoadComp, .def = def);
  asset_repo_source_close(src);
  return;

Error:
  log_e("Failed to load Ftx font-texture", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  data_destroy(g_dataReg, g_alloc_heap, g_dataFtxDefMeta, mem_var(def));
  asset_repo_source_close(src);
}

const AssetFtxChar* asset_ftx_lookup(const AssetFtxComp* comp, const Unicode cp) {
  const AssetFtxChar* ch = search_binary_t(
      comp->characters,
      comp->characters + comp->characterCount,
      AssetFtxChar,
      ftx_compare_char_cp,
      mem_struct(AssetFtxChar, .cp = cp).ptr);

  if (UNLIKELY(!ch)) {
    return &comp->characters[0]; // The 'missing' character, is guaranteed to exist.
  }
  return ch;
}
