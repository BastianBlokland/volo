#pragma once
#include "ecs_module.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

/**
 * ATLas is a combination of a texture atlas and a mapping from names to indices in the atlas.
 */

typedef struct {
  StringHash name;
  u32        atlasIndex;
} AssetAtlasEntry;

ecs_comp_extern_public(AssetAtlasComp) {
  u32              entriesPerDim;
  f32              entryPadding; // Entry padding in fractions of the atlas size.
  AssetAtlasEntry* entries;      // Sorted on the name hash.
  usize            entryCount;
};

/**
 * Get an atlas entry by name.
 * NOTE: Returns null if no entry was found with the given name.
 */
const AssetAtlasEntry* asset_atlas_lookup(const AssetAtlasComp*, StringHash name);

void asset_atlas_jsonschema_write(DynString*);
