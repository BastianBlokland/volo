#include "asset_ftx.h"
#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_utf8.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_register.h"
#include "scene_renderable.h"
#include "scene_text.h"

#define scene_text_tab_size 4
#define scene_text_glyphs_max 2048
#define scene_text_palette_index_bits 2
#define scene_text_palette_size (1 << scene_text_palette_index_bits)
#define scene_text_atlas_index_bits (32 - scene_text_palette_index_bits)
#define scene_text_atlas_index_max ((1 << scene_text_atlas_index_bits) - 1)

static const String g_textGraphic = string_static("graphics/ui/text.gra");
static const String g_textFont    = string_static("fonts/mono.ftx");

typedef struct {
  ALIGNAS(16)
  f32      glyphsPerDim;
  f32      invGlyphsPerDim;
  f32      padding[2];
  GeoColor palette[scene_text_palette_size];
} ShaderFontData;

ASSERT(sizeof(ShaderFontData) == 80, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(8)
  f32 position[2];
  f32 size;
  u32 index; // 2b palette index, 30b glyphIndex.
} ShaderGlyphData;

ASSERT(sizeof(ShaderGlyphData) == 16, "Size needs to match the size defined in glsl");

typedef struct {
  const AssetFtxComp*        font;
  SceneRenderableUniqueComp* renderable;
  GeoColor*                  palette;
  ShaderGlyphData*           outputGlyphData;
  u32                        outputGlyphCount;
  String                     text;
  f32                        glyphSize;
  f32                        startCursor[2];
  f32                        cursor[2];
  u8                         paletteIndex;
} SceneTextBuilder;

static void scene_text_carriage_return(SceneTextBuilder* builder) {
  builder->cursor[0] = builder->startCursor[0];
}

static void scene_text_newline(SceneTextBuilder* builder) {
  scene_text_carriage_return(builder);
  builder->cursor[1] -= (1 + builder->font->lineSpacing) * builder->glyphSize;
}

static void scene_text_next_tabstop_hor(SceneTextBuilder* builder) {
  const AssetFtxChar* space        = asset_ftx_lookup(builder->font, Unicode_Space);
  const f32           spaceAdvance = space->advance * builder->glyphSize;
  const f32           horTabSize   = spaceAdvance * scene_text_tab_size;
  const f32           relCursorX   = builder->cursor[0] - builder->startCursor[0];
  const f32           rem          = math_mod_f32(relCursorX, horTabSize);
  builder->cursor[0] += horTabSize - rem;
}

static void scene_text_build_char(SceneTextBuilder* builder, const Unicode cp) {
  switch (cp) {
  case Unicode_HorizontalTab:
    scene_text_next_tabstop_hor(builder);
    return;
  case Unicode_Newline:
    scene_text_newline(builder);
    return;
  case Unicode_CarriageReturn:
    scene_text_carriage_return(builder);
    return;
  default:
    break;
  }
  const AssetFtxChar* ch = asset_ftx_lookup(builder->font, cp);
  if (!sentinel_check(ch->glyphIndex)) {
    diag_assert(ch->glyphIndex < scene_text_atlas_index_max);
    /**
     * This character has a glyph, output it to the shader.
     */
    builder->outputGlyphData[builder->outputGlyphCount++] = (ShaderGlyphData){
        .position =
            {
                ch->offsetX * builder->glyphSize + builder->cursor[0],
                ch->offsetY * builder->glyphSize + builder->cursor[1],
            },
        .size  = ch->size * builder->glyphSize,
        .index = ch->glyphIndex | (builder->paletteIndex << scene_text_atlas_index_bits),
    };
  }
  builder->cursor[0] += ch->advance * builder->glyphSize;
}

static void scene_text_build(SceneTextBuilder* builder) {
  const usize codePointsCount = utf8_cp_count(builder->text);
  if (UNLIKELY(codePointsCount > scene_text_glyphs_max)) {
    /**
     * NOTE: This check is conservative as not every code-point necessarily has a glyph (for example
     * spaces dont have glyphs).
     */
    log_w(
        "SceneTextComp consists of more codepoints then are supported",
        log_param("codepoints", fmt_int(codePointsCount)),
        log_param("maximum", fmt_int(scene_text_glyphs_max)));
    return;
  }
  const usize maxDataSize = sizeof(ShaderFontData) + sizeof(ShaderGlyphData) * codePointsCount;
  Mem         data        = scene_renderable_unique_data_set(builder->renderable, maxDataSize);

  /**
   * Setup per-font data (shared between all glyphs in this text).
   */
  ShaderFontData* fontData  = mem_as_t(data, ShaderFontData);
  fontData->glyphsPerDim    = builder->font->glyphsPerDim;
  fontData->invGlyphsPerDim = 1.0f / (f32)builder->font->glyphsPerDim;
  mem_cpy(array_mem(fontData->palette), mem_create(builder->palette, sizeof(fontData->palette)));

  /**
   * Build the glyph data.
   */
  builder->outputGlyphData = mem_consume(data, sizeof(ShaderFontData)).ptr;
  do {
    diag_assert(builder->outputGlyphCount < scene_text_glyphs_max);

    Unicode cp;
    builder->text = utf8_cp_read(builder->text, &cp);
    scene_text_build_char(builder, cp);
  } while (!string_is_empty(builder->text));

  /**
   * Finalize the text render data.
   */
  builder->renderable->vertexCountOverride = builder->outputGlyphCount * 6; // 6 verts for a quad.
}

typedef enum {
  SceneGlobalFont_Acquired  = 1 << 0,
  SceneGlobalFont_Unloading = 1 << 1,
} SceneGlobalFontFlags;

typedef enum {
  SceneText_Dirty = 1 << 0,
} SceneTextFlags;

ecs_comp_define(SceneTextComp) {
  SceneTextFlags flags;
  f32            position[2];
  f32            size;
  GeoColor       palette[scene_text_palette_size];
  Mem            textMem;
  usize          textMemSize;
};

ecs_comp_define(SceneGlobalFontComp) {
  EcsEntityId          asset;
  SceneGlobalFontFlags flags;
};

static void ecs_destruct_text(void* data) {
  SceneTextComp* comp = data;
  if (comp->textMem.ptr) {
    alloc_free(g_alloc_heap, comp->textMem);
  }
}

ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(GlobalFontView) { ecs_access_write(SceneGlobalFontComp); }
ecs_view_define(FtxView) { ecs_access_read(AssetFtxComp); }

static AssetManagerComp* scene_asset_manager(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static SceneGlobalFontComp* scene_global_fonts(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalFontView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, SceneGlobalFontComp) : null;
}

static const AssetFtxComp* scene_font_get(EcsWorld* world) {
  SceneGlobalFontComp* globalFonts = scene_global_fonts(world);
  if (!globalFonts) {
    return null; // Global fonts have not been initialized yet.
  }
  if (!(globalFonts->flags & (SceneGlobalFont_Acquired | SceneGlobalFont_Unloading))) {
    log_i("Acquiring global font", log_param("id", fmt_text(g_textFont)));
    asset_acquire(world, globalFonts->asset);
    globalFonts->flags |= SceneGlobalFont_Acquired;
  }

  EcsView* ftxView = ecs_world_view_t(world, FtxView);
  if (!ecs_view_contains(ftxView, globalFonts->asset)) {
    return null; // Ftx font is not loaded yet.
  }
  return ecs_utils_read_t(world, FtxView, globalFonts->asset, AssetFtxComp);
}

ecs_view_define(TextInitView) {
  ecs_access_with(SceneTextComp);
  ecs_access_without(SceneRenderableUniqueComp);
}

ecs_system_define(SceneTextInitSys) {
  AssetManagerComp* assets = scene_asset_manager(world);
  if (!assets) {
    return; // Asset manager hasn't been initialized yet.
  }

  if (!ecs_world_has_t(world, ecs_world_global(world), SceneGlobalFontComp)) {
    // Initialize global fonts lookup.
    const EcsEntityId fontAsset = asset_lookup(world, assets, g_textFont);
    ecs_world_add_t(world, ecs_world_global(world), SceneGlobalFontComp, .asset = fontAsset);
  }

  EcsView* initView = ecs_world_view_t(world, TextInitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    /**
     * Create a 'SceneRenderableUniqueComp' for every text instance.
     */
    ecs_world_add_t(
        world,
        ecs_view_entity(itr),
        SceneRenderableUniqueComp,
        .graphic = asset_lookup(world, assets, g_textGraphic));
  }
}

ecs_system_define(SceneTextUnloadChangedFontsSys) {
  SceneGlobalFontComp* globalFonts = scene_global_fonts(world);
  if (!globalFonts) {
    return;
  }
  const bool isLoaded   = ecs_world_has_t(world, globalFonts->asset, AssetLoadedComp);
  const bool hasChanged = ecs_world_has_t(world, globalFonts->asset, AssetChangedComp);

  if (globalFonts->flags & SceneGlobalFont_Acquired && isLoaded && hasChanged) {
    log_i(
        "Unloading global font",
        log_param("id", fmt_text(g_textFont)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, globalFonts->asset);
    globalFonts->flags &= ~SceneGlobalFont_Acquired;
    globalFonts->flags |= SceneGlobalFont_Unloading;
  }
  if (globalFonts->flags & SceneGlobalFont_Unloading && !isLoaded) {
    globalFonts->flags &= ~SceneGlobalFont_Unloading;
  }
}

ecs_view_define(TextBuildView) {
  ecs_access_write(SceneTextComp);
  ecs_access_write(SceneRenderableUniqueComp);
}

ecs_system_define(SceneTextBuildSys) {
  const AssetFtxComp* ftx = scene_font_get(world);
  if (!ftx) {
    return;
  }
  EcsView* buildView = ecs_world_view_t(world, TextBuildView);
  for (EcsIterator* itr = ecs_view_itr(buildView); ecs_view_walk(itr);) {
    SceneTextComp*             textComp   = ecs_view_write_t(itr, SceneTextComp);
    SceneRenderableUniqueComp* renderable = ecs_view_write_t(itr, SceneRenderableUniqueComp);
    if (!(textComp->flags & SceneText_Dirty)) {
      continue; // Text did not change, no need to rebuild.
    }
    textComp->flags &= ~SceneText_Dirty;

    if (UNLIKELY(!textComp->textMemSize)) {
      renderable->instDataSize        = 0;
      renderable->vertexCountOverride = 0;
      continue;
    }

    scene_text_build(&(SceneTextBuilder){
        .font        = ftx,
        .renderable  = renderable,
        .palette     = textComp->palette,
        .text        = mem_slice(textComp->textMem, 0, textComp->textMemSize),
        .glyphSize   = textComp->size,
        .startCursor = {textComp->position[0], textComp->position[1]},
        .cursor      = {textComp->position[0], textComp->position[1]},
    });
  }
}

ecs_module_init(scene_text_module) {
  ecs_register_comp(SceneTextComp, .destructor = ecs_destruct_text);
  ecs_register_comp(SceneGlobalFontComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GlobalFontView);
  ecs_register_view(FtxView);
  ecs_register_view(TextInitView);
  ecs_register_view(TextBuildView);

  ecs_register_system(SceneTextInitSys, ecs_view_id(GlobalAssetsView), ecs_view_id(TextInitView));
  ecs_register_system(SceneTextUnloadChangedFontsSys, ecs_view_id(GlobalFontView));
  ecs_register_system(
      SceneTextBuildSys,
      ecs_view_id(GlobalFontView),
      ecs_view_id(FtxView),
      ecs_view_id(TextBuildView));

  ecs_order(SceneTextBuildSys, SceneOrder_TextBuild);
}

SceneTextComp* scene_text_add(EcsWorld* world, const EcsEntityId entity) {
  SceneTextComp* text = ecs_world_add_t(world, entity, SceneTextComp, .size = 25);
  array_for_t(text->palette, GeoColor, col) { *col = geo_color_white; }
  return text;
}

void scene_text_update_color(
    SceneTextComp* comp, const SceneTextPalette palette, const GeoColor color) {
  diag_assert(palette < scene_text_palette_size);

  // TODO: Only mark the text as dirty if the color is different.
  comp->flags |= SceneText_Dirty;
  comp->palette[palette] = color;
}

void scene_text_update_position(SceneTextComp* comp, const f32 x, const f32 y) {
  if (comp->position[0] != x || comp->position[1] != y) {
    comp->flags |= SceneText_Dirty;
    comp->position[0] = x;
    comp->position[1] = y;
  }
}

void scene_text_update_size(SceneTextComp* comp, const f32 size) {
  if (comp->size != size) {
    comp->flags |= SceneText_Dirty;
    comp->size = size;
  }
}

void scene_text_update_str(SceneTextComp* comp, const String newText) {
  if (mem_eq(mem_slice(comp->textMem, 0, comp->textMemSize), newText)) {
    // The same string was assigned; no need to rebuild the text.
    return;
  }

  if (UNLIKELY(newText.size > comp->textMem.size)) {
    /**
     * Text does not fit in the existing memory; free the old memory and allocate new memory.
     * NOTE: Rounds the allocation up to the next power-of-two to avoid reallocating many times when
     * slowly growing the text.
     */
    if (LIKELY(comp->textMem.ptr)) {
      alloc_free(g_alloc_heap, comp->textMem);
    }
    comp->textMem = alloc_alloc(g_alloc_heap, bits_nextpow2_64(newText.size), 1);
  }

  comp->flags |= SceneText_Dirty;
  mem_cpy(comp->textMem, newText);
  comp->textMemSize = newText.size;
}
