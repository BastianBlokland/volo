#ifndef INCLUDE_ATLAS
#define INCLUDE_ATLAS

#include "types.glsl"

struct AtlasMeta {
  f32 entriesPerDim;
  f32 entrySize;             // 1.0 / entriesPerDim
  f32 entrySizeMinusPadding; // 1.0 / entriesPerDim - entryPadding * 2.
  f32 entryPadding;
};

/**
 * Compute the x and y position in the texture atlas based on the atlas-index.
 */
f32v2 atlas_entry_origin(const AtlasMeta atlas, const f32 index) {
  const f32 entriesPerDim = atlas.entriesPerDim;
  const f32 entrySize     = atlas.entrySize;
  const f32 entryPadding  = atlas.entryPadding;

  // NOTE: '* entrySize' is equivalent to '/ entriesPerDim'.
  const f32 entryX = mod(index, entriesPerDim) * entrySize + entryPadding;
  const f32 entryY = floor(index * entrySize) * entrySize + entryPadding;
  return f32v2(entryX, entryY);
}

/**
 * Size of an atlas entry.
 * NOTE: Atlas entries are always square.
 */
f32 atlas_entry_size(const AtlasMeta atlas) { return atlas.entrySizeMinusPadding; }

#endif // INCLUDE_ATLAS
