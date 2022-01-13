#include "asset_font.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * TrueType font.
 * Only simple TrueType outlines are supported (no composites at this time).
 * Apple docs: https://developer.apple.com/fonts/TrueType-Reference-Manual/
 * Microsoft docs: https://docs.microsoft.com/en-us/typography/opentype/spec/otff
 */

#define ttf_max_tables 32

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
} TffOffsetTable;

typedef enum {
  TtfError_None = 0,
  TtfError_Malformed,
  TtfError_TooManyTables,
  TtfError_UnsupportedSfntVersion,

  TtfError_Count,
} TtfError;

static String ttf_error_str(TtfError res) {
  static const String msgs[] = {
      string_static("None"),
      string_static("Malformed TrueType font-data"),
      string_static("TrueType font contains more tables then are supported"),
      string_static("Unsupported sfntVersion: Only TrueType outlines are supported"),
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

static Mem ttf_read_table_offset(Mem input, TffOffsetTable* out, TtfError* err) {
  if (UNLIKELY(input.size < 12)) {
    *err = TtfError_Malformed;
    return input;
  }
  *out  = (TffOffsetTable){0};
  input = mem_consume_be_u32(input, &out->sfntVersion);
  input = mem_consume_be_u16(input, &out->numTables);
  input = mem_consume_be_u16(input, &out->searchRange);
  input = mem_consume_be_u16(input, &out->entrySelector);
  input = mem_consume_be_u16(input, &out->rangeShift);

  if (UNLIKELY(out->numTables > ttf_max_tables)) {
    *err = TtfError_TooManyTables;
    return input;
  }
  if (UNLIKELY(input.size < out->numTables * 16)) {
    // Not enough space for records for all the tables.
    *err = TtfError_Malformed;
    return input;
  }
  for (usize i = 0; i != out->numTables; ++i) {
    input = ttf_read_tag(input, &out->records[i].tag, err);
    input = mem_consume_be_u32(input, &out->records[i].checksum);
    input = mem_consume_be_u32(input, &out->records[i].offset);
    input = mem_consume_be_u32(input, &out->records[i].length);
  }
  *err = TtfError_None;
  return input;
}

static void ttf_load_fail(EcsWorld* world, const EcsEntityId assetEntity, const TtfError err) {
  log_e("Failed to parse TrueType font", log_param("error", fmt_text(ttf_error_str(err))));
  ecs_world_add_empty_t(world, assetEntity, AssetFailedComp);
}

void asset_load_ttf(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  Mem      data = src->data;
  TtfError err  = TtfError_None;

  TffOffsetTable offsetTable;
  ttf_read_table_offset(data, &offsetTable, &err);
  if (err) {
    ttf_load_fail(world, assetEntity, err);
    goto Error;
  }
  if (offsetTable.sfntVersion != 0x10000) {
    ttf_load_fail(world, assetEntity, TtfError_UnsupportedSfntVersion);
    goto Error;
  }

  asset_repo_source_close(src);
  ecs_world_add_t(world, assetEntity, AssetFontComp);
  ecs_world_add_empty_t(world, assetEntity, AssetLoadedComp);
  return;

Error:
  asset_repo_source_close(src);
}
