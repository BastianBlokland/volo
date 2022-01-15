#include "asset_font.h"
#include "core_alloc.h"
#include "core_compare.h"
#include "core_diag.h"
#include "core_search.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "repo_internal.h"

ecs_comp_define_public(AssetFontComp);

static void ecs_destruct_font_comp(void* data) {
  AssetFontComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->codepoints, comp->codepointCount);
  alloc_free_array_t(g_alloc_heap, comp->points, comp->pointCount);
  alloc_free_array_t(g_alloc_heap, comp->segments, comp->segmentCount);
  alloc_free_array_t(g_alloc_heap, comp->glyphs, comp->glyphCount);
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetFontComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any font-asset components for unloaded assets.
 */
ecs_system_define(UnloadFontAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetFontComp);
  }
}

ecs_module_init(asset_font_module) {
  ecs_register_comp(AssetFontComp, .destructor = ecs_destruct_font_comp);

  ecs_register_view(UnloadView);

  ecs_register_system(UnloadFontAssetSys, ecs_view_id(UnloadView));
}

i8 asset_font_compare_codepoint(const void* a, const void* b) {
  return compare_u32(
      field_ptr(a, AssetFontCodepoint, unicode), field_ptr(b, AssetFontCodepoint, unicode));
}

const AssetFontGlyph* asset_font_lookup_unicode(const AssetFontComp* font, const u32 unicode) {
  const AssetFontCodepoint* cp = search_binary_t(
      font->codepoints,
      font->codepoints + font->codepointCount,
      AssetFontCodepoint,
      asset_font_compare_codepoint,
      mem_struct(AssetFontCodepoint, .unicode = unicode).ptr);

  if (UNLIKELY(!cp)) {
    return &font->glyphs[0]; // Return the 'missing' glyph (guaranteed to exist).
  }
  diag_assert(cp->glyphIndex < font->glyphCount);
  return &font->glyphs[cp->glyphIndex];
}
