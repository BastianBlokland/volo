#pragma once
#include "core/array.h"
#include "data/registry.h"
#include "ecs/module.h"

/**
 * ATLas is a combination of a texture atlas and a mapping from names to indices in the atlas.
 */

typedef struct {
  StringHash name;
  u32        atlasIndex;
} AssetAtlasEntry;

ecs_comp_extern_public(AssetAtlasComp) {
  u32 entriesPerDim;
  f32 entryPadding;                     // Entry padding in fractions of the atlas size.
  HeapArray_t(AssetAtlasEntry) entries; // Sorted on the name hash.
};

extern DataMeta g_assetAtlasBundleMeta;
extern DataMeta g_assetAtlasDefMeta;
extern DataMeta g_assetAtlasMeta;

/**
 * Get an atlas entry by name.
 * NOTE: Returns null if no entry was found with the given name.
 */
const AssetAtlasEntry* asset_atlas_lookup(const AssetAtlasComp*, StringHash name);
