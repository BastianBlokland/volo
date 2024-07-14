#include "asset_font.h"
#include "asset_fonttex.h"
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
#include "data_schema.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

/**
 * FontTexture - Generates a sdf texture atlas and a character mapping based on a font file.
 */

#define fonttex_max_chars 1024
#define fonttex_max_size (1024 * 16)
#define fonttex_max_fonts 100

static DataReg* g_dataReg;
static DataMeta g_dataFontTexDefMeta;

typedef enum {
  FontTexGenFlags_IncludeGlyph0 = 1 << 0, // Aka the '.notdef' glyph or the 'missing glyph'.
} FontTexGenFlags;

typedef struct {
  String      id;
  u8          variation;
  EcsEntityId asset;
  f32         yOffset;
  f32         spacing;
  String      characters;
} FontTexDefFont;

typedef struct {
  u32 size, glyphSize;
  u32 border;
  f32 lineSpacing;
  f32 baseline;
  struct {
    FontTexDefFont* values;
    usize           count;
  } fonts;
} FontTexDef;

static void fonttex_datareg_init(void) {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_allocPersist);

    // clang-format off
    data_reg_struct_t(reg, FontTexDefFont);
    data_reg_field_t(reg, FontTexDefFont, id, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, FontTexDefFont, variation, data_prim_t(u8), .flags = DataFlags_Opt);
    data_reg_field_t(reg, FontTexDefFont, yOffset, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, FontTexDefFont, spacing, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, FontTexDefFont, characters, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, FontTexDef);
    data_reg_field_t(reg, FontTexDef, size, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, FontTexDef, glyphSize, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, FontTexDef, border, data_prim_t(u32));
    data_reg_field_t(reg, FontTexDef, lineSpacing, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, FontTexDef, baseline, data_prim_t(f32));
    data_reg_field_t(reg, FontTexDef, fonts, t_FontTexDefFont, .container = DataContainer_Array, .flags = DataFlags_NotEmpty);
    // clang-format on

    g_dataFontTexDefMeta = data_meta_t(t_FontTexDef);
    g_dataReg            = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetFontTexComp);

ecs_comp_define(AssetFontTexLoadComp) { FontTexDef def; };

static void ecs_destruct_fonttex_comp(void* data) {
  AssetFontTexComp* comp = data;
  alloc_free_array_t(g_allocHeap, comp->characters, comp->characterCount);
}

static void ecs_destruct_fonttex_load_comp(void* data) {
  AssetFontTexLoadComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_dataFontTexDefMeta, mem_var(comp->def));
}

typedef enum {
  FontTexError_None = 0,
  FontTexError_FontInvalid,
  FontTexError_FontGlyphMissing,
  FontTexError_SizeNonPow2,
  FontTexError_SizeTooBig,
  FontTexError_GlyphSizeNonPow2,
  FontTexError_TooManyFonts,
  FontTexError_TooManyCharacters,
  FontTexError_TooManyGlyphs,
  FontTexError_InvalidUtf8,

  FontTexError_Count,
} FontTexError;

static String fonttex_error_str(const FontTexError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("FontTex specifies an invalid font"),
      string_static("FontTex source font is missing a glyph for the requested characters"),
      string_static("FontTex specifies a non power-of-two texture size"),
      string_static("FontTex specifies a texture size larger then is supported"),
      string_static("FontTex specifies a non power-of-two glyph size"),
      string_static("FontTex specifies more fonts then are supported"),
      string_static("FontTex specifies more characters then are supported"),
      string_static("FontTex requires more glyphs then fit at the requested size"),
      string_static("FontTex specifies invalid utf8"),
  };
  ASSERT(array_elems(g_msgs) == FontTexError_Count, "Incorrect number of fonttex-error messages");
  return g_msgs[err];
}

static i8 fonttex_compare_char_cp(const void* a, const void* b) {
  const AssetFontTexChar* charA  = a;
  const AssetFontTexChar* charB  = b;
  const i8                result = charA->cp < charB->cp ? -1 : charA->cp > charB->cp ? 1 : 0;
  if (LIKELY(result != 0)) {
    return result;
  }
  return charA->variation < charB->variation ? -1 : charA->variation > charB->variation ? 1 : 0;
}

typedef struct {
  u32                   cp;
  const AssetFontGlyph* glyph;
} FontTexDefChar;

static u32 fonttex_lookup_chars(
    const AssetFontComp*  font,
    const FontTexGenFlags flags,
    String                chars,
    FontTexDefChar        out[fonttex_max_chars],
    FontTexError*         err) {

  u32 index = 0;
  if (flags & FontTexGenFlags_IncludeGlyph0) {
    out[index++] = (FontTexDefChar){.cp = 0, .glyph = asset_font_missing(font)};
  }

  do {
    Unicode cp;
    chars = utf8_cp_read(chars, &cp);
    if (UNLIKELY(index >= fonttex_max_chars)) {
      *err = FontTexError_TooManyCharacters;
      return 0;
    }
    if (UNLIKELY(!cp)) {
      *err = FontTexError_InvalidUtf8;
      return 0;
    }
    const AssetFontGlyph* glyph = asset_font_lookup(font, cp);
    if (UNLIKELY(glyph == asset_font_missing(font))) {
      *err = FontTexError_FontGlyphMissing;
      return 0;
    }
    out[index++] = (FontTexDefChar){.cp = cp, .glyph = glyph};

  } while (chars.size);

  *err = FontTexError_None;
  return index;
}

static void fonttex_generate_glyph(
    const FontTexDef*     def,
    const AssetFontComp*  font,
    const AssetFontGlyph* glyph,
    const u32             index,
    u8*                   out) {

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
      out[texPixelY * def->size + texPixelX] = value;
    }
  }
}

typedef struct {
  const AssetFontComp* data;
  u8                   variation;
  f32                  yOffset;
  f32                  spacing;
  String               characters;
} FontTexDefResolvedFont;

static void fonttex_generate_font(
    const FontTexDef*            def,
    const FontTexDefResolvedFont font,
    const FontTexGenFlags        flags,
    u32                          maxGlyphs,
    u16*                         nextGlyphIndex,
    DynArray*                    outChars, // AssetFtxChar[]
    u8*                          outPixels,
    FontTexError*                err) {

  FontTexDefChar inputChars[fonttex_max_chars];
  const u32 charCount = fonttex_lookup_chars(font.data, flags, font.characters, inputChars, err);
  if (UNLIKELY(*err)) {
    return;
  }

  for (u32 i = 0; i != charCount; ++i) {
    *dynarray_push_t(outChars, AssetFontTexChar) = (AssetFontTexChar){
        .cp         = inputChars[i].cp,
        .variation  = font.variation,
        .glyphIndex = inputChars[i].glyph->segmentCount ? *nextGlyphIndex : sentinel_u16,
        .size       = inputChars[i].glyph->size,
        .offsetX    = inputChars[i].glyph->offsetX,
        .offsetY    = inputChars[i].glyph->offsetY + font.yOffset,
        .advance    = inputChars[i].glyph->advance + font.spacing,
        .border     = def->border / (f32)def->glyphSize,
    };
    if (inputChars[i].glyph->segmentCount) {
      if (UNLIKELY(*nextGlyphIndex >= maxGlyphs || *nextGlyphIndex == u16_max)) {
        *err = FontTexError_TooManyGlyphs;
        return;
      }
      fonttex_generate_glyph(def, font.data, inputChars[i].glyph, (*nextGlyphIndex)++, outPixels);
    }
  }
}

static void fonttex_generate(
    const FontTexDef*             def,
    const FontTexDefResolvedFont* fonts,
    const u32                     fontCount,
    AssetFontTexComp*             outFontTex,
    AssetTextureComp*             outTexture,
    FontTexError*                 err) {

  Mem pixelMem = alloc_alloc(g_allocHeap, def->size * def->size, 1);
  mem_set(pixelMem, 0xFF); // Initialize to the maximum distance away from a glyph.

  u8*      pixels = pixelMem.ptr;
  DynArray chars  = dynarray_create_t(g_allocHeap, AssetFontTexChar, 128);

  const u32 glyphsPerDim   = def->size / def->glyphSize;
  const u32 maxGlyphs      = glyphsPerDim * glyphsPerDim;
  u16       nextGlyphIndex = 0;
  if (UNLIKELY(!maxGlyphs)) {
    *err = FontTexError_TooManyGlyphs;
    goto Error;
  }

  for (u32 i = 0; i != fontCount; ++i) {
    const FontTexGenFlags genFlags = i == 0 ? FontTexGenFlags_IncludeGlyph0 : 0;

    fonttex_generate_font(def, fonts[i], genFlags, maxGlyphs, &nextGlyphIndex, &chars, pixels, err);
    if (UNLIKELY(*err)) {
      goto Error;
    }
  }

  // Sort the characters on the unicode codepoint.
  dynarray_sort(&chars, fonttex_compare_char_cp);

  *outFontTex = (AssetFontTexComp){
      .glyphsPerDim   = glyphsPerDim,
      .lineSpacing    = def->lineSpacing,
      .baseline       = def->baseline,
      .characters     = dynarray_copy_as_new(&chars, g_allocHeap),
      .characterCount = chars.size,
  };
  *outTexture = (AssetTextureComp){
      .type         = AssetTextureType_U8,
      .channels     = AssetTextureChannels_One,
      .pixelsRaw    = pixels,
      .width        = def->size,
      .height       = def->size,
      .layers       = 1,
      .srcMipLevels = 1,
  };
  dynarray_destroy(&chars);
  *err = FontTexError_None;
  return;

Error:
  diag_assert(*err);
  dynarray_destroy(&chars);
  alloc_free_array_t(g_allocHeap, pixels, def->size * def->size);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(LoadView) {
  ecs_access_read(AssetComp);
  ecs_access_write(AssetFontTexLoadComp);
}

ecs_view_define(FontView) { ecs_access_write(AssetFontComp); }

/**
 * Update all active loads.
 */
ecs_system_define(FontTexLoadAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView*     loadView = ecs_world_view_t(world, LoadView);
  EcsIterator* fontItr  = ecs_view_itr(ecs_world_view_t(world, FontView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId     entity = ecs_view_entity(itr);
    const String          id     = asset_id(ecs_view_read_t(itr, AssetComp));
    AssetFontTexLoadComp* load   = ecs_view_write_t(itr, AssetFontTexLoadComp);
    FontTexError          err;

    const u32               fontCount = (u32)load->def.fonts.count;
    FontTexDefResolvedFont* fonts     = mem_stack(sizeof(FontTexDefResolvedFont) * fontCount).ptr;
    for (u32 i = 0; i != fontCount; ++i) {
      FontTexDefFont* defFont = &load->def.fonts.values[i];
      if (!defFont->asset) {
        defFont->asset = asset_lookup(world, manager, defFont->id);
        asset_acquire(world, defFont->asset);
        asset_register_dep(world, entity, defFont->asset);
      }
      if (ecs_world_has_t(world, defFont->asset, AssetFailedComp)) {
        err = FontTexError_FontInvalid;
        goto Error;
      }
      if (!ecs_world_has_t(world, defFont->asset, AssetLoadedComp)) {
        goto Wait; // Wait for the font to load.
      }
      if (UNLIKELY(!ecs_view_maybe_jump(fontItr, defFont->asset))) {
        err = FontTexError_FontInvalid;
        goto Error;
      }
      fonts[i] = (FontTexDefResolvedFont){
          .data       = ecs_view_read_t(fontItr, AssetFontComp),
          .variation  = defFont->variation,
          .yOffset    = defFont->yOffset,
          .spacing    = defFont->spacing,
          .characters = defFont->characters,
      };
    }

    AssetFontTexComp ftx;
    AssetTextureComp texture;
    fonttex_generate(&load->def, fonts, fontCount, &ftx, &texture, &err);
    if (UNLIKELY(err)) {
      goto Error;
    }

    *ecs_world_add_t(world, entity, AssetFontTexComp) = ftx;
    *ecs_world_add_t(world, entity, AssetTextureComp) = texture;
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e(
        "Failed to load font-texture",
        log_param("id", fmt_text(id)),
        log_param("error", fmt_text(fonttex_error_str(err))));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    ecs_world_remove_t(world, entity, AssetFontTexLoadComp);
    array_ptr_for_t(load->def.fonts, FontTexDefFont, font) {
      if (font->asset) {
        asset_release(world, font->asset);
      }
    }

  Wait:
    continue;
  }
}

ecs_view_define(FontTexUnloadView) {
  ecs_access_with(AssetFontTexComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any fonttex-asset component for unloaded assets.
 */
ecs_system_define(FontTexUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, FontTexUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetFontTexComp);
  }
}

ecs_module_init(asset_fonttex_module) {
  fonttex_datareg_init();

  ecs_register_comp(AssetFontTexComp, .destructor = ecs_destruct_fonttex_comp);
  ecs_register_comp(AssetFontTexLoadComp, .destructor = ecs_destruct_fonttex_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(FontView);
  ecs_register_view(FontTexUnloadView);

  ecs_register_system(
      FontTexLoadAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView), ecs_view_id(FontView));

  ecs_register_system(FontTexUnloadAssetSys, ecs_view_id(FontTexUnloadView));
}

void asset_load_fonttex(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  String         errMsg;
  FontTexDef     def;
  DataReadResult result;
  data_read_json(g_dataReg, src->data, g_allocHeap, g_dataFontTexDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(!bits_ispow2(def.size))) {
    errMsg = fonttex_error_str(FontTexError_SizeNonPow2);
    goto Error;
  }
  if (UNLIKELY(def.size > fonttex_max_size)) {
    errMsg = fonttex_error_str(FontTexError_SizeTooBig);
    goto Error;
  }
  if (UNLIKELY(!bits_ispow2(def.glyphSize))) {
    errMsg = fonttex_error_str(FontTexError_GlyphSizeNonPow2);
    goto Error;
  }
  if (UNLIKELY(def.fonts.count > fonttex_max_fonts)) {
    errMsg = fonttex_error_str(FontTexError_TooManyFonts);
    goto Error;
  }

  ecs_world_add_t(world, entity, AssetFontTexLoadComp, .def = def);
  asset_repo_source_close(src);
  return;

Error:
  log_e(
      "Failed to load font-texture",
      log_param("id", fmt_text(id)),
      log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  data_destroy(g_dataReg, g_allocHeap, g_dataFontTexDefMeta, mem_var(def));
  asset_repo_source_close(src);
}

const AssetFontTexChar*
asset_fonttex_lookup(const AssetFontTexComp* comp, const Unicode cp, const u8 variation) {

  /**
   * Binary scan to find a character with a matching code-point.
   * Looks for a character with the same variation otherwise variation 0 is returned.
   */
  const AssetFontTexChar* begin      = comp->characters;
  const AssetFontTexChar* end        = comp->characters + comp->characterCount;
  const AssetFontTexChar* matchingCp = null;
  while (begin < end) {
    const usize             elems  = end - begin;
    const AssetFontTexChar* middle = begin + (elems / 2);
    if (middle->cp == cp) {
      if (middle->variation == variation) {
        return middle;
      }
      matchingCp = middle;
    }
    if (middle->cp > cp || (middle->cp == cp && middle->variation > variation)) {
      end = middle; // Disregard everything after (and including) middle.
    } else {
      begin = middle + 1; // Discard everything before (and including) middle.
    }
  }
  if (matchingCp) {
    /**
     * Preferred variation was not found, walk backwards to variation 0 and return that.
     */
    for (; matchingCp->variation; --matchingCp)
      ;
    return matchingCp;
  }
  // Return the 'missing' character, is guaranteed to exist.
  return &comp->characters[0];
}

void asset_fonttex_jsonschema_write(DynString* str) {
  fonttex_datareg_init();

  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_dataFontTexDefMeta, schemaFlags);
}
