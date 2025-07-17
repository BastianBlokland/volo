#include "core_array.h"

#include "format_internal.h"

typedef struct {
  String      assetId;
  StringHash  assetIdHash;
  AssetFormat format;
  u32         region;
  u32         offset, size; // Within the region.
} AssetPackEntry;

typedef struct {
  usize offset, size; // Byte into the file.
} AssetPackRegion;

typedef struct {
  HeapArray_t(AssetPackEntry) entries;
  HeapArray_t(AssetPackRegion) regions;
} AssetPackHeader;

i8 asset_pack_compare_entry(const void* a, const void* b);
