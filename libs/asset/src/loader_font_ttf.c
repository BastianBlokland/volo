#include "asset_font.h"
#include "core_alloc.h"
#include "core_annotation.h"
#include "core_array.h"
#include "core_bits.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

OPTIMIZE_OFF();

/**
 * TrueType font.
 * Only simple TrueType outlines are supported (no composites at this time).
 * Apple docs: https://developer.apple.com/fonts/TrueType-Reference-Manual/
 * Microsoft docs: https://docs.microsoft.com/en-us/typography/opentype/spec/otff
 *
 * Ttf fonts use big-endian and 2's complement integers.
 * NOTE: This loader assumes the host system is also using 2's complement integers.
 */

#define ttf_max_tables 32
#define ttf_magic 0x5F0F3CF5

typedef struct {
  String tag;
  u32    checksum;
  u32    offset;
  u32    length;
} TtfTableRecord;

typedef struct {
  u32            sfntVersion;
  u16            numTables;
  u16            searchRange;
  u16            entrySelector;
  u16            rangeShift;
  TtfTableRecord records[ttf_max_tables];
} TtfOffsetTable;

typedef struct {
  u16 majorVersion, minorVersion;
  f32 fontRevision;
  u32 checksumAdjustment;
  u32 magicNumber;
  u16 flags;
  u16 unitsPerEm;
  i64 dateCreated, dateModified;
  i16 glyphMinX, glyphMinY, glyphMaxX, glyphMaxY;
  u16 macStyle;
  u16 lowestRecPpem;
  i16 fontDirectionHint;
  i16 indexToLocFormat;
  i16 glyphDataFormat;
} TtfHeadTable;

typedef struct {
  f32 version;
  u16 numGlyphs;
  u16 maxPoints;
  u16 maxContours;
  u16 maxCompositePoints;
  u16 maxCompositeContours;
  u16 maxZones;
  u16 maxTwilightPoints;
  u16 maxStorage;
  u16 maxFunctionDefs;
  u16 maxInstructionDefs;
  u16 maxStackElements;
  u16 maxSizeOfInstructions;
  u16 maxComponentElements;
  u16 maxComponentDepth;
} TtfMaxpTable;

typedef enum {
  TtfError_None = 0,
  TtfError_Malformed,
  TtfError_TooManyTables,
  TtfError_UnsupportedSfntVersion,
  TtfError_UnalignedTable,
  TtfError_TableChecksumFailed,
  TtfError_TableDataMissing,
  TtfError_HeadTableMissing,
  TtfError_HeadTableMalformed,
  TtfError_HeadTableUnsupported,
  TtfError_MaxpTableMissing,

  TtfError_Count,
} TtfError;

static String ttf_error_str(TtfError res) {
  static const String msgs[] = {
      string_static("None"),
      string_static("Malformed TrueType font-data"),
      string_static("TrueType font contains more tables then are supported"),
      string_static("Unsupported sfntVersion: Only TrueType outlines are supported"),
      string_static("Unaligned TrueType table"),
      string_static("TrueType table checksum failed"),
      string_static("TrueType table data missing"),
      string_static("TrueType head table missing"),
      string_static("TrueType head table malformed"),
      string_static("TrueType head table unsupported"),
      string_static("TrueType maxp table missing"),
  };
  ASSERT(array_elems(msgs) == TtfError_Count, "Incorrect number of ttf-error messages");
  return msgs[res];
}

/**
 * Four character string used to identify tables.
 * More info: https://docs.microsoft.com/en-us/typography/opentype/spec/otff#data-types
 */
static Mem ttf_read_tag(Mem input, String* out, TtfError* err) {
  if (UNLIKELY(input.size < 4)) {
    *err = TtfError_Malformed;
    return input;
  }
  *out = string_slice(input, 0, 4);
  *err = TtfError_None;
  return string_consume(input, 4);
}

/**
 * Read a 32 bit signed fixed-point number (16.16).
 */
static Mem ttf_read_fixed(Mem input, f32* out) {
  i32 value;
  input = mem_consume_be_u32(input, (u32*)&value); // NOTE: Interpret as 2's complement.
  *out  = value / (f32)(1 << 16);
  return input;
}

static void ttf_read_offset_table(Mem data, TtfOffsetTable* out, TtfError* err) {
  if (UNLIKELY(data.size < 12)) {
    *err = TtfError_Malformed;
    return;
  }
  *out = (TtfOffsetTable){0};
  data = mem_consume_be_u32(data, &out->sfntVersion);
  data = mem_consume_be_u16(data, &out->numTables);
  data = mem_consume_be_u16(data, &out->searchRange);
  data = mem_consume_be_u16(data, &out->entrySelector);
  data = mem_consume_be_u16(data, &out->rangeShift);

  if (UNLIKELY(out->numTables > ttf_max_tables)) {
    *err = TtfError_TooManyTables;
    return;
  }
  if (UNLIKELY(data.size < out->numTables * 16)) {
    *err = TtfError_Malformed;
    return;
  }
  for (usize i = 0; i != out->numTables; ++i) {
    data = ttf_read_tag(data, &out->records[i].tag, err);
    data = mem_consume_be_u32(data, &out->records[i].checksum);
    data = mem_consume_be_u32(data, &out->records[i].offset);
    data = mem_consume_be_u32(data, &out->records[i].length);
  }
  *err = TtfError_None;
  return;
}

static const TtfTableRecord* ttf_find_table(TtfOffsetTable* offsetTable, const String tag) {
  for (usize i = 0; i != offsetTable->numTables; ++i) {
    const TtfTableRecord* record = &offsetTable->records[i];
    if (string_eq(record->tag, tag)) {
      return record;
    }
  }
  return null;
}

static void
ttf_read_head_table(Mem data, TtfOffsetTable* offsetTable, TtfHeadTable* out, TtfError* err) {
  const TtfTableRecord* tableRecord = ttf_find_table(offsetTable, string_lit("head"));
  if (UNLIKELY(!tableRecord)) {
    *err = TtfError_HeadTableMissing;
    return;
  }
  data = mem_slice(data, tableRecord->offset, tableRecord->length);
  if (UNLIKELY(data.size < 54)) {
    *err = TtfError_Malformed;
    return;
  }
  /**
   * NOTE: For signed values we assume the host system is using 2's complement integers.
   */
  *out = (TtfHeadTable){0};
  data = mem_consume_be_u16(data, &out->majorVersion);
  data = mem_consume_be_u16(data, &out->minorVersion);
  data = ttf_read_fixed(data, &out->fontRevision);
  data = mem_consume_be_u32(data, &out->checksumAdjustment);
  data = mem_consume_be_u32(data, &out->magicNumber);
  data = mem_consume_be_u16(data, &out->flags);
  data = mem_consume_be_u16(data, &out->unitsPerEm);
  data = mem_consume_be_u64(data, (u64*)&out->dateCreated);
  data = mem_consume_be_u64(data, (u64*)&out->dateModified);
  data = mem_consume_be_u16(data, (u16*)&out->glyphMinX);
  data = mem_consume_be_u16(data, (u16*)&out->glyphMinY);
  data = mem_consume_be_u16(data, (u16*)&out->glyphMaxX);
  data = mem_consume_be_u16(data, (u16*)&out->glyphMaxY);
  data = mem_consume_be_u16(data, &out->macStyle);
  data = mem_consume_be_u16(data, &out->lowestRecPpem);
  data = mem_consume_be_u16(data, (u16*)&out->fontDirectionHint);
  data = mem_consume_be_u16(data, (u16*)&out->indexToLocFormat);
  data = mem_consume_be_u16(data, (u16*)&out->glyphDataFormat);
}

static void
ttf_read_maxp_table(Mem data, TtfOffsetTable* offsetTable, TtfMaxpTable* out, TtfError* err) {
  const TtfTableRecord* tableRecord = ttf_find_table(offsetTable, string_lit("maxp"));
  if (UNLIKELY(!tableRecord)) {
    *err = TtfError_HeadTableMissing;
    return;
  }
  data = mem_slice(data, tableRecord->offset, tableRecord->length);
  if (UNLIKELY(data.size < 32)) {
    *err = TtfError_Malformed;
    return;
  }
  *out = (TtfMaxpTable){0};
  data = ttf_read_fixed(data, &out->version);
  data = mem_consume_be_u16(data, &out->numGlyphs);
  data = mem_consume_be_u16(data, &out->maxPoints);
  data = mem_consume_be_u16(data, &out->maxContours);
  data = mem_consume_be_u16(data, &out->maxCompositePoints);
  data = mem_consume_be_u16(data, &out->maxCompositeContours);
  data = mem_consume_be_u16(data, &out->maxZones);
  data = mem_consume_be_u16(data, &out->maxTwilightPoints);
  data = mem_consume_be_u16(data, &out->maxStorage);
  data = mem_consume_be_u16(data, &out->maxFunctionDefs);
  data = mem_consume_be_u16(data, &out->maxInstructionDefs);
  data = mem_consume_be_u16(data, &out->maxStackElements);
  data = mem_consume_be_u16(data, &out->maxSizeOfInstructions);
  data = mem_consume_be_u16(data, &out->maxComponentElements);
  data = mem_consume_be_u16(data, &out->maxComponentDepth);
}

/**
 * Calculate the checksum of the input data.
 * Both offset and length have to be aligned to a 4 byte boundary.
 * More info: https://docs.microsoft.com/en-us/typography/opentype/spec/otff#calculating-checksums
 */
static u32 ttf_checksum(Mem data) {
  if (!bits_aligned_ptr(data.ptr, 4) || !bits_aligned(data.size, 4)) {
    return 0;
  }
  u32 checksum = 0;
  while (data.size) {
    u32 value;
    data = mem_consume_be_u32(data, &value);
    checksum += value;
  }
  return checksum;
}

static void ttf_validate(Mem data, const TtfOffsetTable* offsetTable, TtfError* err) {
  for (usize i = 0; i != offsetTable->numTables; ++i) {
    const TtfTableRecord* record = &offsetTable->records[i];
    if (!bits_aligned(record->offset, 4)) {
      *err = TtfError_UnalignedTable;
      return;
    }
    const u32 alignedLength = bits_align(record->length, 4);
    if (data.size < record->offset + alignedLength) {
      // Not enough data is available for this table.
      // NOTE: ttf tables have to be padded to the next 4 byte boundary.
      *err = TtfError_TableDataMissing;
      return;
    }
    if (string_eq(record->tag, string_lit("head"))) {
      // TODO: Validate head table checksum, for the head table the checksum works differently as
      // it contains a checksum adjustment for the entire font.
      continue;
    }
    if (ttf_checksum(mem_slice(data, record->offset, alignedLength)) != record->checksum) {
      *err = TtfError_TableChecksumFailed;
      return;
    }
  }
  *err = TtfError_None;
}

static void ttf_load_fail(EcsWorld* world, const EcsEntityId assetEntity, const TtfError err) {
  log_e("Failed to parse TrueType font", log_param("error", fmt_text(ttf_error_str(err))));
  ecs_world_add_empty_t(world, assetEntity, AssetFailedComp);
}

void asset_load_ttf(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  Mem      data = src->data;
  TtfError err  = TtfError_None;

  TtfOffsetTable offsetTable;
  ttf_read_offset_table(data, &offsetTable, &err);
  if (err) {
    ttf_load_fail(world, assetEntity, err);
    goto Error;
  }
  if (offsetTable.sfntVersion != 0x10000) {
    ttf_load_fail(world, assetEntity, TtfError_UnsupportedSfntVersion);
    goto Error;
  }
  ttf_validate(data, &offsetTable, &err);
  if (err) {
    ttf_load_fail(world, assetEntity, err);
    goto Error;
  }
  TtfHeadTable headTable;
  ttf_read_head_table(data, &offsetTable, &headTable, &err);
  if (err) {
    ttf_load_fail(world, assetEntity, err);
    goto Error;
  }
  if (headTable.magicNumber != ttf_magic) {
    ttf_load_fail(world, assetEntity, TtfError_HeadTableMalformed);
    goto Error;
  }
  if (headTable.majorVersion != 0 && headTable.majorVersion != 1) {
    ttf_load_fail(world, assetEntity, TtfError_HeadTableUnsupported);
    goto Error;
  }
  if (headTable.fontDirectionHint != 2) {
    ttf_load_fail(world, assetEntity, TtfError_HeadTableUnsupported);
    goto Error;
  }
  TtfMaxpTable maxpTable;
  ttf_read_maxp_table(data, &offsetTable, &maxpTable, &err);
  if (err) {
    ttf_load_fail(world, assetEntity, err);
    goto Error;
  }

  asset_repo_source_close(src);
  ecs_world_add_t(world, assetEntity, AssetFontComp);
  ecs_world_add_empty_t(world, assetEntity, AssetLoadedComp);
  return;

Error:
  asset_repo_source_close(src);
}
