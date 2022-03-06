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

#include "manager_internal.h"
#include "repo_internal.h"

/**
 * FontTeXture - Generates a sdf texture atlas and a character mapping based on a font file.
 */

#define ftx_max_chars 1024
#define ftx_max_size (1024 * 16)
#define ftx_max_fonts 100

static DataReg* g_dataReg;
static DataMeta g_dataFtxDefMeta;

typedef enum {
  FtxGenFlags_IncludeGlyph0 = 1 << 0, // Aka the '.notdef' glyph or the 'missing glyph'.
} FtxGenFlags;

typedef struct {
  String      id;
  EcsEntityId asset;
  f32         yOffset;
  String      characters;
} FtxDefFont;

typedef struct {
  u32 size, glyphSize;
  u32 border;
  f32 lineSpacing;
  struct {
    FtxDefFont* values;
    usize       count;
  } fonts;
} FtxDef;

static void ftx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, FtxDefFont);
    data_reg_field_t(g_dataReg, FtxDefFont, id, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, FtxDefFont, yOffset, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, FtxDefFont, characters, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, FtxDef);
    data_reg_field_t(g_dataReg, FtxDef, size, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, FtxDef, glyphSize, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, FtxDef, border, data_prim_t(u32));
    data_reg_field_t(g_dataReg, FtxDef, lineSpacing, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, FtxDef, fonts, t_FtxDefFont, .container = DataContainer_Array, .flags = DataFlags_NotEmpty);
    // clang-format on

    g_dataFtxDefMeta = data_meta_t(t_FtxDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetFtxComp);

ecs_comp_define(AssetFtxLoadComp) { FtxDef def; };

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
  FtxError_FontInvalid,
  FtxError_FontGlyphMissing,
  FtxError_SizeNonPow2,
  FtxError_SizeTooBig,
  FtxError_GlyphSizeNonPow2,
  FtxError_TooManyFonts,
  FtxError_TooManyCharacters,
  FtxError_TooManyGlyphs,
  FtxError_InvalidUtf8,

  FtxError_Count,
} FtxError;

static String ftx_error_str(const FtxError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Ftx specifies an invalid font"),
      string_static("Ftx source font is missing a glyph for the requested characters"),
      string_static("Ftx specifies a non power-of-two texture size"),
      string_static("Ftx specifies a texture size larger then is supported"),
      string_static("Ftx specifies a non power-of-two glyph size"),
      string_static("Ftx specifies more fonts then are supported"),
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
} FtxDefChar;

static u32 ftx_lookup_chars(
    const AssetFontComp* font,
    const FtxGenFlags    flags,
    String               chars,
    FtxDefChar           out[ftx_max_chars],
    FtxError*            err) {

  u32 index = 0;
  if (flags & FtxGenFlags_IncludeGlyph0) {
    out[index++] = (FtxDefChar){.cp = 0, .glyph = asset_font_missing(font)};
  }

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
    out[index++] = (FtxDefChar){.cp = cp, .glyph = glyph};

  } while (chars.size);

  *err = FtxError_None;
  return index;
}

static void ftx_generate_glyph(
    const FtxDef*         def,
    const AssetFontComp*  font,
    const AssetFontGlyph* glyph,
    const u32             index,
    AssetTexturePixelB1*  out) {

  const u32 texY = index * def->glyphSize / def->size * def->glyphSize;
  const u32 texX = index * def->glyphSize % def->size;

  diag_assert(texY + def->glyphSize <= def->size);
  diag_assert(texX + def->glyphSize <= def->size);

  const u32 glyphSize    = def->glyphSize;
  const f32 invGlyphSize = 1.0f / glyphSize;
  const f32 border       = def->border * invGlyphSize / glyph->size;
  const f32 invBorder    = 1.0f / border;
  const f32 scale        = 1.0f + border * 2.0f;

  for (usize glyphPixelY = 0; glyphPixelY != glyphSize; ++glyphPixelY) {
    for (usize glyphPixelX = 0; glyphPixelX != glyphSize; ++glyphPixelX) {
      const AssetFontPoint point = {
          .x = ((glyphPixelX + 0.5f) * invGlyphSize * scale - border),
          .y = ((glyphPixelY + 0.5f) * invGlyphSize * scale - border),
      };
      const f32 dist       = asset_font_glyph_dist(font, glyph, point);
      const f32 borderFrac = math_clamp_f32(dist * invBorder, -1.0f, 1.0f);
      const u8  value      = (u8)((borderFrac * 0.5f + 0.5f) * 255.999f);

      const usize texPixelY                  = texY + glyphPixelY;
      const usize texPixelX                  = texX + glyphPixelX;
      out[texPixelY * def->size + texPixelX] = (AssetTexturePixelB1){value};
    }
  }
}

typedef struct {
  const AssetFontComp* data;
  f32                  yOffset;
  String               characters;
} FtxDefResolvedFont;

static void ftx_generate_font(
    const FtxDef*            def,
    const FtxDefResolvedFont font,
    const FtxGenFlags        flags,
    u32                      maxGlyphs,
    u32*                     nextGlyphIndex,
    DynArray*                outChars, // AssetFtxChar[]
    AssetTexturePixelB1*     outPixels,
    FtxError*                err) {

  FtxDefChar inputChars[ftx_max_chars];
  const u32  charCount = ftx_lookup_chars(font.data, flags, font.characters, inputChars, err);
  if (UNLIKELY(*err)) {
    return;
  }

  for (u32 i = 0; i != charCount; ++i) {
    *dynarray_push_t(outChars, AssetFtxChar) = (AssetFtxChar){
        .cp         = inputChars[i].cp,
        .glyphIndex = inputChars[i].glyph->segmentCount ? *nextGlyphIndex : sentinel_u32,
        .size       = inputChars[i].glyph->size,
        .offsetX    = inputChars[i].glyph->offsetX,
        .offsetY    = inputChars[i].glyph->offsetY + font.yOffset,
        .advance    = inputChars[i].glyph->advance,
        .border     = def->border / (f32)def->glyphSize,
    };
    if (inputChars[i].glyph->segmentCount) {
      if (UNLIKELY(*nextGlyphIndex >= maxGlyphs)) {
        *err = FtxError_TooManyGlyphs;
        return;
      }
      ftx_generate_glyph(def, font.data, inputChars[i].glyph, (*nextGlyphIndex)++, outPixels);
    }
  }
}

static void ftx_generate(
    const FtxDef*             def,
    const FtxDefResolvedFont* fonts,
    const u32                 fontCount,
    AssetFtxComp*             outFtx,
    AssetTextureComp*         outTexture,
    FtxError*                 err) {

  Mem pixelMem = alloc_alloc(g_alloc_heap, sizeof(AssetTexturePixelB1) * def->size * def->size, 1);
  mem_set(pixelMem, 0xFF); // Initialize to the maximum distance away from a glyph.

  AssetTexturePixelB1* pixels = pixelMem.ptr;
  DynArray             chars  = dynarray_create_t(g_alloc_heap, AssetFtxChar, 128);

  const u32 glyphsPerDim   = def->size / def->glyphSize;
  const u32 maxGlyphs      = glyphsPerDim * glyphsPerDim;
  u32       nextGlyphIndex = 0;
  if (UNLIKELY(!maxGlyphs)) {
    *err = FtxError_TooManyGlyphs;
    goto Error;
  }

  for (u32 i = 0; i != fontCount; ++i) {
    const FtxGenFlags genFlags = i == 0 ? FtxGenFlags_IncludeGlyph0 : 0;

    ftx_generate_font(def, fonts[i], genFlags, maxGlyphs, &nextGlyphIndex, &chars, pixels, err);
    if (UNLIKELY(*err)) {
      goto Error;
    }
  }

  // Sort the characters on the unicode codepoint.
  dynarray_sort(&chars, ftx_compare_char_cp);

  *outFtx = (AssetFtxComp){
      .glyphsPerDim   = glyphsPerDim,
      .lineSpacing    = def->lineSpacing,
      .characters     = dynarray_copy_as_new(&chars, g_alloc_heap),
      .characterCount = chars.size,
  };
  *outTexture = (AssetTextureComp){
      .type     = AssetTextureType_Byte,
      .channels = AssetTextureChannels_One,
      .pixelsB1 = pixels,
      .width    = def->size,
      .height   = def->size,
  };
  dynarray_destroy(&chars);
  *err = FtxError_None;
  return;

Error:
  diag_assert(*err);
  dynarray_destroy(&chars);
  alloc_free_array_t(g_alloc_heap, pixels, def->size * def->size);
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
  EcsView*     loadView = ecs_world_view_t(world, LoadView);
  EcsIterator* fontItr  = ecs_view_itr(ecs_world_view_t(world, FontView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    AssetFtxLoadComp* load   = ecs_view_write_t(itr, AssetFtxLoadComp);
    FtxError          err;

    const u32           fontCount = (u32)load->def.fonts.count;
    FtxDefResolvedFont* fonts     = mem_stack(sizeof(FtxDefResolvedFont) * fontCount).ptr;
    for (u32 i = 0; i != fontCount; ++i) {
      FtxDefFont* defFont = &load->def.fonts.values[i];
      if (!defFont->asset) {
        defFont->asset = asset_lookup(world, manager, defFont->id);
        asset_acquire(world, defFont->asset);
        asset_register_dep(world, entity, defFont->asset);
      }
      if (ecs_world_has_t(world, defFont->asset, AssetFailedComp)) {
        err = FtxError_FontInvalid;
        goto Error;
      }
      if (!ecs_world_has_t(world, defFont->asset, AssetLoadedComp)) {
        goto Wait; // Wait for the font to load.
      }
      if (UNLIKELY(!ecs_view_maybe_jump(fontItr, defFont->asset))) {
        err = FtxError_FontInvalid;
        goto Error;
      }
      fonts[i] = (FtxDefResolvedFont){
          .data       = ecs_view_read_t(fontItr, AssetFontComp),
          .yOffset    = defFont->yOffset,
          .characters = defFont->characters,
      };
    }

    AssetFtxComp     ftx;
    AssetTextureComp texture;
    ftx_generate(&load->def, fonts, fontCount, &ftx, &texture, &err);
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
    array_ptr_for_t(load->def.fonts, FtxDefFont, font) {
      if (font->asset) {
        asset_release(world, font->asset);
      }
    }

  Wait:
    continue;
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

void asset_load_ftx(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  String         errMsg;
  FtxDef         def;
  DataReadResult result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataFtxDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
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
  if (UNLIKELY(def.fonts.count > ftx_max_fonts)) {
    errMsg = ftx_error_str(FtxError_TooManyFonts);
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
