#include "asset_ftx.h"
#include "asset_manager.h"
#include "core_diag.h"
#include "core_unicode.h"
#include "core_utf8.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_renderable.h"
#include "scene_text.h"

#define scene_text_max_glyphs 2048

static const String g_textGraphic = string_static("graphics/ui/text.gra");
static const String g_textFont    = string_static("fonts/mono.ftx");

typedef struct {
  ALIGNAS(16)
  f32 glyphsPerDim;
  f32 invGlyphsPerDim;
} ShaderFontData;

ASSERT(sizeof(ShaderFontData) == 16, "Size needs to match the size defined in glsl");

typedef struct {
  ALIGNAS(16)
  f32 position[2];
  f32 size;
  f32 index;
} ShaderCharData;

ASSERT(sizeof(ShaderCharData) == 16, "Size needs to match the size defined in glsl");

typedef struct {
  const AssetFtxComp*        font;
  SceneRenderableUniqueComp* renderable;
  ShaderCharData*            outputCharData;
  u32                        outputGlyphCount;
  String                     text;
  f32                        cursor[2];
  f32                        glyphSize;
} SceneTextBuilder;

static void scene_text_build_char(SceneTextBuilder* builder, const UnicodeCp cp) {
  const AssetFtxChar* ch = asset_ftx_lookup(builder->font, cp);
  if (!sentinel_check(ch->glyphIndex)) {
    /**
     * This character has a glyph, output it to the shader.
     */
    builder->outputCharData[builder->outputGlyphCount++] = (ShaderCharData){
        .index = (f32)ch->glyphIndex,
        .position =
            {
                ch->offsetX * builder->glyphSize + builder->cursor[0],
                ch->offsetY * builder->glyphSize + builder->cursor[1],
            },
        .size = ch->size * builder->glyphSize,
    };
  }
  builder->cursor[0] += ch->advance * builder->glyphSize;
}

static void scene_text_build(SceneTextBuilder* builder) {
  const usize codePointsCount = utf8_cp_count(builder->text);
  if (UNLIKELY(codePointsCount > scene_text_max_glyphs)) {
    /**
     * NOTE: This check is conservative as not every code-point necessarily has a glyph (for example
     * spaces dont have glyphs).
     */
    log_w(
        "SceneTextComp consists of more codepoints then are supported",
        log_param("codepoints", fmt_int(codePointsCount)),
        log_param("maximum", fmt_int(scene_text_max_glyphs)));
    return;
  }

  const usize maxDataSize = sizeof(ShaderFontData) + sizeof(ShaderCharData) * codePointsCount;
  Mem         data        = scene_renderable_unique_data(builder->renderable, maxDataSize);

  /**
   * Setup per-font data (shared between all glyphs in this text).
   */
  *mem_as_t(data, ShaderFontData) = (ShaderFontData){
      .glyphsPerDim    = (f32)builder->font->glyphsPerDim,
      .invGlyphsPerDim = 1.0f / (f32)builder->font->glyphsPerDim,
  };

  /**
   * Build the glyph data.
   */
  builder->outputCharData = mem_consume(data, sizeof(ShaderFontData)).ptr;
  do {
    diag_assert(builder->outputGlyphCount < scene_text_max_glyphs);

    UnicodeCp cp;
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

ecs_comp_define_public(SceneTextComp);
ecs_comp_define(SceneGlobalFontComp) {
  EcsEntityId          asset;
  SceneGlobalFontFlags flags;
};

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

  EcsView* renderView = ecs_world_view_t(world, TextInitView);
  for (EcsIterator* itr = ecs_view_itr(renderView); ecs_view_walk(itr);) {
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

ecs_view_define(TextRenderView) {
  ecs_access_read(SceneTextComp);
  ecs_access_write(SceneRenderableUniqueComp);
}

ecs_system_define(SceneTextRenderSys) {
  const AssetFtxComp* ftx = scene_font_get(world);
  if (!ftx) {
    return;
  }
  EcsView* renderView = ecs_world_view_t(world, TextRenderView);
  for (EcsIterator* itr = ecs_view_itr(renderView); ecs_view_walk(itr);) {
    const SceneTextComp* textComp = ecs_view_read_t(itr, SceneTextComp);

    scene_text_build(&(SceneTextBuilder){
        .font       = ftx,
        .renderable = ecs_view_write_t(itr, SceneRenderableUniqueComp),
        .text       = textComp->text,
        .cursor     = {textComp->x, textComp->y},
        .glyphSize  = 100.0f,
    });
  }
}

ecs_module_init(scene_text_module) {
  ecs_register_comp(SceneTextComp);
  ecs_register_comp(SceneGlobalFontComp);

  ecs_register_view(GlobalAssetsView);
  ecs_register_view(GlobalFontView);
  ecs_register_view(FtxView);
  ecs_register_view(TextInitView);
  ecs_register_view(TextRenderView);

  ecs_register_system(SceneTextInitSys, ecs_view_id(GlobalAssetsView), ecs_view_id(TextInitView));
  ecs_register_system(SceneTextUnloadChangedFontsSys, ecs_view_id(GlobalFontView));
  ecs_register_system(
      SceneTextRenderSys,
      ecs_view_id(GlobalFontView),
      ecs_view_id(FtxView),
      ecs_view_id(TextRenderView));
}
