#include "core/dynarray.h"

#include "format.h"

typedef struct {
  String      id;
  StringHash  idHash;
  AssetFormat format;
  u32         checksum; // crc32 (ISO 3309).
  u16         region;
  u32         offset, size; // Within the region.
} AssetPackEntry;

typedef struct {
  u64 offset; // Bytes into the file.
  u32 size;
  u32 checksum; // crc32 (ISO 3309).
} AssetPackRegion;

typedef struct {
  DynArray entries; // AssetPackEntry[], sorted on idHash.
  DynArray regions; // AssetPackRegion[]
} AssetPackHeader;

extern DataMeta g_assetPackMeta;

i8 asset_pack_compare_entry(const void* a, const void* b);
