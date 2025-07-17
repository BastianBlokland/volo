#include "core_dynarray.h"

#include "format_internal.h"

typedef struct {
  String      id;
  StringHash  idHash;
  AssetFormat format;
  u32         region;
  u32         offset, size; // Within the region.
} AssetPackEntry;

typedef struct {
  u64 offset, size; // Bytes into the file.
} AssetPackRegion;

typedef struct {
  DynArray entries; // AssetPackEntry[], sorted on idHash.
  DynArray regions; // AssetPackRegion[]
} AssetPackHeader;

extern DataMeta g_assetPackMeta;

i8 asset_pack_compare_entry(const void* a, const void* b);
