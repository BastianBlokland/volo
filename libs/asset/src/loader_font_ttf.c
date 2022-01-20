#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "loader_font_internal.h"
#include "repo_internal.h"

/**
 * TrueType font.
 * Only simple TrueType outlines are supported (no composites at this time).
 * Apple docs: https://developer.apple.com/fonts/TrueType-Reference-Manual/
 * Microsoft docs: https://docs.microsoft.com/en-us/typography/opentype/spec/otff
 *
 * Ttf fonts use big-endian and 2's complement integers.
 * NOTE: This loader assumes the host system is also using 2's complement integers.
 */

#define ttf_magic 0x5F0F3CF5
#define ttf_supported_sfnt_version 0x10000
#define ttf_max_tables 32
#define ttf_max_encodings 16
#define ttf_max_glyphs 15000
#define ttf_max_contours_per_glyph 128
#define ttf_max_points_per_glyph 512

ASSERT(
    ttf_max_glyphs* ttf_max_points_per_glyph < u32_max,
    "Points should be safely indexable using 32 bits");

typedef struct {
  String tag;
  u32    checksum;
  Mem    data;
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
  f32 invUnitsPerEm;
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

typedef struct {
  u16 platformId;
  u16 encodingId;
  Mem data;
} TtfEncodingRecord;

typedef struct {
  u16               version;
  u16               numEncodings;
  TtfEncodingRecord encodings[ttf_max_encodings];
} TtfCmapTable;

typedef struct {
  u16    language; // Unused as we only support unicode (non language specific).
  u16    segCount;
  u16    searchRange;
  u16    entrySelector;
  u16    rangeShift;
  u16*   endCodes;   // u16[segCount]
  u16*   startCodes; // u16[segCount]
  u16*   deltas;     // u16[segCount]
  void** rangeData;  // void*[segCount]
} TtfCmapFormat4Header;

typedef struct {
  f32 version;
  i16 ascent, descent;
  i16 lineGap;
  u16 advanceWidthMax;
  i16 minLeftSideBearing, maxLeftSideBearing;
  i16 xMaxExtent;
  i16 caretSlopeRise, caretSlopeRun, caretOffset;
  i16 metricDataFormat;
  u16 numOfLongHorMetrics;
} TtfHheaTable;

typedef struct {
  u16 advanceWidth;
  i16 leftSideBearing;
} TtfGlyphHorMetrics;

typedef struct {
  i16 numContours;
  f32 gridOriginX, gridOriginY; // Origin of the ttf grid.
  f32 gridScale;                // Scale to multiply grid ttf points by to normalize them.
  f32 size;                     // Size of the glyph.
  f32 offsetX, offsetY;         // Offset of the glyph.
} TtfGlyphHeader;

typedef enum {
  TtfGlyphFlags_OnCurvePoint             = 1 << 0,
  TtfGlyphFlags_XShortVector             = 1 << 1,
  TtfGlyphFlags_YShortVector             = 1 << 2,
  TtfGlyphFlags_Repeat                   = 1 << 3,
  TtfGlyphFlags_XIsSameOrPositiveXVector = 1 << 4,
  TtfGlyphFlags_YIsSameOrPositiveYVector = 1 << 5,
  TtfGlyphFlags_OverlapSimple            = 1 << 6,
} TtfGlyphFlags;

typedef enum {
  TtfError_None = 0,
  TtfError_Malformed,
  TtfError_TooManyTables,
  TtfError_TooManyEncodings,
  TtfError_TooManyGlyphs,
  TtfError_TooManyContours,
  TtfError_TooManyPoints,
  TtfError_UnsupportedSfntVersion,
  TtfError_UnalignedTable,
  TtfError_TableChecksumFailed,
  TtfError_TableDataMissing,
  TtfError_HeadTableMissing,
  TtfError_HeadTableMalformed,
  TtfError_HeadTableUnsupported,
  TtfError_MaxpTableMissing,
  TtfError_CmapTableMissing,
  TtfError_CmapNoSupportedEncoding,
  TtfError_CmapFormat4EncodingMalformed,
  TtfError_HheaTableMissing,
  TtfError_HmtxTableMissing,
  TtfError_HmtxTableMalformed,
  TtfError_NoCharacters,
  TtfError_NoGlyphPoints,
  TtfError_NoGlyphSegments,
  TtfError_LocaTableMissing,
  TtfError_LocaTableMissingGlyphs,
  TtfError_LocaTableGlyphOutOfBounds,
  TtfError_GlyfTableMissing,
  TtfError_GlyfTableEntryHeaderMalformed,
  TtfError_GlyfTableEntryPointsMalformed,
  TtfError_GlyfTableEntryContourMalformed,
  TtfError_GlyfTableEntryMalformed,

  TtfError_Count,
} TtfError;

static String ttf_error_str(TtfError res) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Malformed TrueType font-data"),
      string_static("TrueType font contains more tables then are supported"),
      string_static("TrueType font contains more encodings then are supported"),
      string_static("TrueType font contains more glyphs then are supported"),
      string_static("TrueType glyph contains more contours then are supported"),
      string_static("TrueType glyph contains more points then are supported"),
      string_static("Unsupported sfntVersion: Only TrueType outlines are supported"),
      string_static("Unaligned TrueType table"),
      string_static("TrueType table checksum failed"),
      string_static("TrueType table data missing"),
      string_static("TrueType head table missing"),
      string_static("TrueType head table malformed"),
      string_static("TrueType head table unsupported"),
      string_static("TrueType maxp table missing"),
      string_static("TrueType cmap table missing"),
      string_static("TrueType cmap table does not contain any supported encodings"),
      string_static("TrueType cmap table format4 encoding malformed"),
      string_static("TrueType hhea table missing"),
      string_static("TrueType hmtx table missing"),
      string_static("TrueType hmtx table is malformed"),
      string_static("TrueType font contains no characters"),
      string_static("TrueType font contains no glyph points"),
      string_static("TrueType font contains no glyph segments"),
      string_static("TrueType loca table missing"),
      string_static("TrueType loca table does not contain locations for all glyphs"),
      string_static("TrueType loca table specifies out-of-bounds glyph data"),
      string_static("TrueType glyf table missing"),
      string_static("TrueType glyf table entry header malformed"),
      string_static("TrueType glyf table entry points malformed"),
      string_static("TrueType glyf table entry contains a malformed contour"),
      string_static("TrueType glyf table entry malformed"),

  };
  ASSERT(array_elems(g_msgs) == TtfError_Count, "Incorrect number of ttf-error messages");
  return g_msgs[res];
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
  const Mem fileData = data;

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

    u32 tableOffset, tableLength;
    data = mem_consume_be_u32(data, &tableOffset);
    data = mem_consume_be_u32(data, &tableLength);
    if (UNLIKELY(!bits_aligned(tableOffset, 4))) {
      *err = TtfError_UnalignedTable;
      return;
    }
    tableLength = bits_align(tableLength, 4); // All tables have to be 4 byte aligned.
    if (UNLIKELY(tableOffset + tableLength > fileData.size)) {
      *err = TtfError_TableDataMissing;
      return;
    }
    out->records[i].data = mem_slice(fileData, tableOffset, tableLength);
  }
  *err = TtfError_None;
  return;
}

static const TtfTableRecord* ttf_find_table(const TtfOffsetTable* offsetTable, const String tag) {
  for (usize i = 0; i != offsetTable->numTables; ++i) {
    const TtfTableRecord* record = &offsetTable->records[i];
    if (string_eq(record->tag, tag)) {
      return record;
    }
  }
  return null;
}

static void
ttf_read_head_table(const TtfOffsetTable* offsetTable, TtfHeadTable* out, TtfError* err) {
  const TtfTableRecord* tableRecord = ttf_find_table(offsetTable, string_lit("head"));
  if (UNLIKELY(!tableRecord)) {
    *err = TtfError_HeadTableMissing;
    return;
  }
  Mem data = tableRecord->data;
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

  out->invUnitsPerEm = 1.0f / (f32)out->unitsPerEm;
  *err               = TtfError_None;
}

static void
ttf_read_maxp_table(const TtfOffsetTable* offsetTable, TtfMaxpTable* out, TtfError* err) {
  const TtfTableRecord* tableRecord = ttf_find_table(offsetTable, string_lit("maxp"));
  if (UNLIKELY(!tableRecord)) {
    *err = TtfError_HeadTableMissing;
    return;
  }
  Mem data = tableRecord->data;
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
  *err = TtfError_None;
}

static void
ttf_read_cmap_table(const TtfOffsetTable* offsetTable, TtfCmapTable* out, TtfError* err) {
  const TtfTableRecord* tableRecord = ttf_find_table(offsetTable, string_lit("cmap"));
  if (UNLIKELY(!tableRecord)) {
    *err = TtfError_CmapTableMissing;
    return;
  }
  Mem data = tableRecord->data;
  if (UNLIKELY(data.size < 4)) {
    *err = TtfError_Malformed;
    return;
  }

  *out = (TtfCmapTable){0};
  data = mem_consume_be_u16(data, &out->version);
  data = mem_consume_be_u16(data, &out->numEncodings);
  if (UNLIKELY(out->numEncodings > ttf_max_encodings)) {
    *err = TtfError_TooManyEncodings;
    return;
  }
  if (UNLIKELY(data.size < out->numEncodings * 8)) {
    *err = TtfError_Malformed;
    return;
  }
  for (usize i = 0; i != out->numEncodings; ++i) {
    data = mem_consume_be_u16(data, &out->encodings[i].platformId);
    data = mem_consume_be_u16(data, &out->encodings[i].encodingId);
    u32 offset;
    data                   = mem_consume_be_u32(data, &offset);
    out->encodings[i].data = mem_consume(tableRecord->data, offset);
  }
  *err = TtfError_None;
  return;
}

static void ttf_read_cmap_format4_header(Mem data, TtfCmapFormat4Header* out, TtfError* err) {
  if (UNLIKELY(data.size < 10)) {
    *err = TtfError_CmapFormat4EncodingMalformed;
    return;
  }
  *out = (TtfCmapFormat4Header){0};
  data = mem_consume_be_u16(data, &out->language);
  u16 doubleSegCount;
  data          = mem_consume_be_u16(data, &doubleSegCount);
  out->segCount = doubleSegCount / 2;
  data          = mem_consume_be_u16(data, &out->searchRange);
  data          = mem_consume_be_u16(data, &out->entrySelector);
  data          = mem_consume_be_u16(data, &out->rangeShift);
  if (UNLIKELY(data.size < (usize)(2 + out->segCount * 8))) {
    *err = TtfError_CmapFormat4EncodingMalformed;
    return;
  }
  out->endCodes   = alloc_array_t(g_alloc_heap, u16, out->segCount);
  out->startCodes = alloc_array_t(g_alloc_heap, u16, out->segCount);
  out->deltas     = alloc_array_t(g_alloc_heap, u16, out->segCount);
  out->rangeData  = alloc_array_t(g_alloc_heap, void*, out->segCount);
  // Read endCodes.
  for (usize i = 0; i != out->segCount; ++i) {
    data = mem_consume_be_u16(data, &out->endCodes[i]);
  }
  data = mem_consume(data, 2);
  // Read startCodes.
  for (usize i = 0; i != out->segCount; ++i) {
    data = mem_consume_be_u16(data, &out->startCodes[i]);
  }
  // Read deltas.
  for (usize i = 0; i != out->segCount; ++i) {
    data = mem_consume_be_u16(data, &out->deltas[i]);
  }
  // Read rangeOffsets.
  for (usize i = 0; i != out->segCount; ++i) {
    u16 rangeOffset;
    data = mem_consume_be_u16(data, &rangeOffset);
    // Range offsets are offsets from the current location in the file.
    out->rangeData[i] = rangeOffset ? bits_ptr_offset(data.ptr, rangeOffset - 2) : null;
  }
  *err = TtfError_None;
}

static void ttf_read_characters_format4(
    Mem                 data,
    const TtfMaxpTable* maxpTable,
    DynArray*           out, // AssetFontChar[], needs to be already initialized.
    TtfError*           err) {
  TtfCmapFormat4Header header;
  ttf_read_cmap_format4_header(data, &header, err);
  if (UNLIKELY(*err)) {
    return;
  }

  // Iterate over every segment (block of codepoints) and map the characters to glyphs.
  for (usize segIdx = 0U; segIdx != header.segCount; ++segIdx) {
    const u16   startCode = header.startCodes[segIdx];
    const u16   endCode   = header.endCodes[segIdx];
    const u16   delta     = header.deltas[segIdx];
    const void* rangeData = header.rangeData[segIdx];
    if (UNLIKELY(startCode == 0xFFFF || endCode == 0xFFFF)) {
      continue; // 0xFFFF is used as a stop sentinel.
    }
    for (u16 code = startCode; code <= endCode; ++code) {
      /**
       * There are two different ways of mapping segments to glyphs, either a direct mapping (with
       * an offset) or a lookup table.
       */
      if (rangeData) {
        // Read the glyph-id from a lookup table.
        const Mem glyphIndexMem = mem_create(bits_ptr_offset(rangeData, (code - startCode) * 2), 2);
        if (UNLIKELY(mem_end(glyphIndexMem) > mem_end(data))) {
          *err = TtfError_CmapFormat4EncodingMalformed;
          goto End;
        }
        u16 glyphIndex;
        mem_consume_be_u16(glyphIndexMem, &glyphIndex);
        if (LIKELY(glyphIndex < maxpTable->numGlyphs)) {
          *dynarray_push_t(out, AssetFontChar) = (AssetFontChar){code, glyphIndex};
        }
      } else {
        // Directly map a code-point to a glyph (with a offset named 'delta').
        const u16 glyphIndex = (code + delta) % u16_lit(65536);
        if (LIKELY(glyphIndex < maxpTable->numGlyphs)) {
          *dynarray_push_t(out, AssetFontChar) = (AssetFontChar){code, glyphIndex};
        }
      }
    }
  }
  *err = TtfError_None;
End:
  alloc_free_array_t(g_alloc_heap, header.endCodes, header.segCount);
  alloc_free_array_t(g_alloc_heap, header.startCodes, header.segCount);
  alloc_free_array_t(g_alloc_heap, header.deltas, header.segCount);
  alloc_free_array_t(g_alloc_heap, header.rangeData, header.segCount);
}

static void ttf_read_characters(
    const TtfCmapTable* cmapTable,
    const TtfMaxpTable* maxpTable,
    DynArray*           out, // AssetFontChar[], needs to be already initialized.
    TtfError*           err) {
  for (usize i = 0; i != cmapTable->numEncodings; ++i) {
    const TtfEncodingRecord* encoding = &cmapTable->encodings[i];
    Mem                      data     = encoding->data;
    if (UNLIKELY(data.size < 4)) {
      continue;
    }
    u16 formatNumber;
    data = mem_consume_be_u16(data, &formatNumber);
    switch (formatNumber) {
    case 4: {
      u16 formatDataSize;
      data = mem_consume_be_u16(data, &formatDataSize);
      if (UNLIKELY((usize)(formatDataSize - 4) > data.size)) {
        *err = TtfError_CmapFormat4EncodingMalformed;
        return;
      }
      data = mem_slice(data, 0, formatDataSize - 4);
      ttf_read_characters_format4(data, maxpTable, out, err);
      return;
    }
    }
  }
  *err = TtfError_CmapNoSupportedEncoding;
}

static void
ttf_read_hhea_table(const TtfOffsetTable* offsetTable, TtfHheaTable* out, TtfError* err) {
  const TtfTableRecord* tableRecord = ttf_find_table(offsetTable, string_lit("hhea"));
  if (UNLIKELY(!tableRecord)) {
    *err = TtfError_HheaTableMissing;
    return;
  }
  Mem data = tableRecord->data;
  if (UNLIKELY(data.size < 36)) {
    *err = TtfError_Malformed;
    return;
  }
  /**
   * NOTE: For signed values we assume the host system is using 2's complement integers.
   */
  *out = (TtfHheaTable){0};
  data = ttf_read_fixed(data, &out->version);
  data = mem_consume_be_u16(data, (u16*)&out->ascent);
  data = mem_consume_be_u16(data, (u16*)&out->descent);
  data = mem_consume_be_u16(data, (u16*)&out->lineGap);
  data = mem_consume_be_u16(data, &out->advanceWidthMax);
  data = mem_consume_be_u16(data, (u16*)&out->minLeftSideBearing);
  data = mem_consume_be_u16(data, (u16*)&out->maxLeftSideBearing);
  data = mem_consume_be_u16(data, (u16*)&out->xMaxExtent);
  data = mem_consume_be_u16(data, (u16*)&out->caretSlopeRise);
  data = mem_consume_be_u16(data, (u16*)&out->caretSlopeRun);
  data = mem_consume_be_u16(data, (u16*)&out->caretOffset);
  data = mem_consume(data, 8);
  data = mem_consume_be_u16(data, (u16*)&out->metricDataFormat);
  data = mem_consume_be_u16(data, &out->numOfLongHorMetrics);
  *err = TtfError_None;
  return;
}

static void ttf_read_glyph_locations(
    const TtfOffsetTable* offsetTable,
    const TtfMaxpTable*   maxpTable,
    const TtfHeadTable*   headTable,
    Mem*                  out, // Mem[maxpTable.numGlyphs]
    TtfError*             err) {
  const TtfTableRecord* locaTableRec = ttf_find_table(offsetTable, string_lit("loca"));
  if (UNLIKELY(!locaTableRec)) {
    *err = TtfError_LocaTableMissing;
    return;
  }
  const TtfTableRecord* glyfTableRec = ttf_find_table(offsetTable, string_lit("glyf"));
  if (UNLIKELY(!glyfTableRec)) {
    *err = TtfError_GlyfTableMissing;
    return;
  }
  Mem locaData = locaTableRec->data;
  Mem glyfData = glyfTableRec->data;
  switch (headTable->indexToLocFormat) {
  case 1: {
    /**
     * Long version of the loca table (32 bit offsets).
     */
    if (UNLIKELY(locaData.size < (usize)(maxpTable->numGlyphs * 4 + 1))) { // +1 for the end offset.
      *err = TtfError_LocaTableMissingGlyphs;
      return;
    }
    for (usize i = 0; i <= maxpTable->numGlyphs; ++i) { // +1 for the end-offset.
      u32 offset;
      locaData     = mem_consume_be_u32(locaData, &offset);
      u8* startPtr = bits_ptr_offset(glyfData.ptr, offset);
      if (LIKELY(i != maxpTable->numGlyphs)) {
        out[i].ptr = startPtr;
      }
      if (LIKELY(i)) {
        out[i - 1].size = startPtr - mem_begin(out[i - 1]);
        if (UNLIKELY(out[i - 1].size > glyfData.size)) {
          *err = TtfError_LocaTableGlyphOutOfBounds;
          return;
        }
      }
    }
  } break;
  default: {
    /**
     * Short version of the loca table (16 bit offsets divided by two).
     */
    if (UNLIKELY(locaData.size < (usize)(maxpTable->numGlyphs * 2 + 1))) { // +1 for the end offset.
      *err = TtfError_LocaTableMissingGlyphs;
      return;
    }
    for (usize i = 0; i <= maxpTable->numGlyphs; ++i) { // +1 for the end-offset.
      u16 offsetDiv2;
      locaData     = mem_consume_be_u16(locaData, &offsetDiv2);
      u8* startPtr = bits_ptr_offset(glyfData.ptr, offsetDiv2 * 2);
      if (LIKELY(i != maxpTable->numGlyphs)) {
        out[i].ptr = startPtr;
      }
      if (LIKELY(i)) {
        out[i - 1].size = startPtr - mem_begin(out[i - 1]);
        if (UNLIKELY(out[i - 1].size > glyfData.size)) {
          *err = TtfError_LocaTableGlyphOutOfBounds;
          return;
        }
      }
    }
  } break;
  }
  *err = TtfError_None;
}

static void ttf_read_glyph_hor_metrics(
    const TtfOffsetTable* offsetTable,
    const TtfMaxpTable*   maxpTable,
    const TtfHheaTable*   hheaTable,
    TtfGlyphHorMetrics*   out, // TtfGlyphHorMetrics[maxpTable.numGlyphs]
    TtfError*             err) {
  const TtfTableRecord* tableRecord = ttf_find_table(offsetTable, string_lit("hmtx"));
  if (UNLIKELY(!tableRecord)) {
    *err = TtfError_HmtxTableMissing;
    return;
  }
  Mem data = tableRecord->data;

  // Read the 'long' entries (both a advanceWidth and a leftSideBearing).
  if (UNLIKELY(data.size < hheaTable->numOfLongHorMetrics * 4)) {
    *err = TtfError_HmtxTableMalformed;
    return;
  }
  if (UNLIKELY(hheaTable->numOfLongHorMetrics > maxpTable->numGlyphs)) {
    *err = TtfError_Malformed;
    return;
  }
  for (usize i = 0; i != hheaTable->numOfLongHorMetrics; ++i) {
    data = mem_consume_be_u16(data, &out[i].advanceWidth);
    data = mem_consume_be_u16(data, (u16*)&out[i].leftSideBearing);
  }

  // Read the 'short' entries (only a leftSideBearing, advanceWith of the last long entry is used).
  const u16 remainingEntries = maxpTable->numGlyphs - hheaTable->numOfLongHorMetrics;
  if (UNLIKELY(data.size < remainingEntries * 2)) {
    *err = TtfError_HmtxTableMalformed;
    return;
  }
  const u16 lastLongIndex = hheaTable->numOfLongHorMetrics ? hheaTable->numOfLongHorMetrics - 1 : 0;
  for (usize i = 0; i != remainingEntries; ++i) {
    data = mem_consume_be_u16(data, (u16*)&out[lastLongIndex + i].leftSideBearing);
    out[lastLongIndex + i].advanceWidth = out[lastLongIndex].advanceWidth;
  }
}

static Mem
ttf_read_glyph_header(Mem data, const TtfHeadTable* headTable, TtfGlyphHeader* out, TtfError* err) {

  if (UNLIKELY(data.size < 10)) {
    *err = TtfError_GlyfTableEntryHeaderMalformed;
    return data;
  }

  /**
   * NOTE: For signed values we assume the host system is using 2's complement integers.
   */
  *out = (TtfGlyphHeader){0};
  i16 gridMinX, gridMinY, gridMaxX, gridMaxY;
  data = mem_consume_be_u16(data, (u16*)&out->numContours);
  data = mem_consume_be_u16(data, (u16*)&gridMinX);
  data = mem_consume_be_u16(data, (u16*)&gridMinY);
  data = mem_consume_be_u16(data, (u16*)&gridMaxX);
  data = mem_consume_be_u16(data, (u16*)&gridMaxY);

  const u16 gridWidth  = gridMaxX - gridMinX;
  const u16 gridHeight = gridMaxY - gridMinY;
  const u16 gridSize   = math_max(gridWidth, gridHeight);
  out->gridOriginX     = (f32)gridMinX;
  out->gridOriginY     = (f32)gridMinY;
  out->gridScale       = gridSize ? 1.0f / gridSize : 0.0f;
  out->size            = gridSize * headTable->invUnitsPerEm;
  out->offsetX         = gridMinX * headTable->invUnitsPerEm;
  out->offsetY         = gridMinY * headTable->invUnitsPerEm;

  *err = TtfError_None;
  return data;
}

static Mem ttf_read_glyph_flags(Mem data, const usize count, u8* out, TtfError* err) {
  for (usize i = 0; i != count;) {
    if (UNLIKELY(!data.size)) {
      *err = TtfError_GlyfTableEntryMalformed;
      return data;
    }
    u8 flag;
    data           = mem_consume_u8(data, &flag);
    u8 repeatCount = 0;
    if (flag & TtfGlyphFlags_Repeat) {
      data = mem_consume_u8(data, &repeatCount);
      if (UNLIKELY(!repeatCount)) {
        *err = TtfError_GlyfTableEntryMalformed;
        return data;
      }
    }
    out[i++] = flag;
    while (repeatCount--) {
      out[i++] = flag;
    }
  }
  *err = TtfError_None;
  return data;
}

static Mem ttf_read_glyph_points(
    Mem                   data,
    const TtfGlyphHeader* header,
    const u8*             flags,
    const usize           count,
    AssetFontPoint*       out,
    TtfError*             err) {

  // Read the x coordinates for all points.
  i32 xPos = 0;
  for (usize i = 0; i != count; ++i) {
    if (flags[i] & TtfGlyphFlags_XShortVector) {
      if (UNLIKELY(!data.size)) {
        *err = TtfError_GlyfTableEntryPointsMalformed;
        return data;
      }
      u8 offset;
      data = mem_consume_u8(data, &offset);
      xPos += offset * (flags[i] & TtfGlyphFlags_XIsSameOrPositiveXVector ? 1 : -1);
    } else {
      if (UNLIKELY(data.size < 2)) {
        *err = TtfError_GlyfTableEntryPointsMalformed;
        return data;
      }
      i16 offset = 0;
      if (!(flags[i] & TtfGlyphFlags_XIsSameOrPositiveXVector)) {
        data = mem_consume_be_u16(data, (u16*)&offset);
      }
      xPos += offset;
    }
    out[i].x = (xPos - header->gridOriginX) * header->gridScale;
  }

  // Read the y coordinates for all points.
  i32 yPos = 0;
  for (usize i = 0; i != count; ++i) {
    if (flags[i] & TtfGlyphFlags_YShortVector) {
      if (UNLIKELY(!data.size)) {
        *err = TtfError_GlyfTableEntryPointsMalformed;
        return data;
      }
      u8 offset;
      data = mem_consume_u8(data, &offset);
      yPos += offset * (flags[i] & TtfGlyphFlags_YIsSameOrPositiveYVector ? 1 : -1);
    } else {
      i16 offset = 0;
      if (!(flags[i] & TtfGlyphFlags_YIsSameOrPositiveYVector)) {
        if (UNLIKELY(data.size < 2)) {
          *err = TtfError_GlyfTableEntryPointsMalformed;
          return data;
        }
        data = mem_consume_be_u16(data, (u16*)&offset);
      }
      yPos += offset;
    }
    out[i].y = (yPos - header->gridOriginY) * header->gridScale;
  }

  *err = TtfError_None;
  return data;
}

/**
 * Construct a glyph out of the ttf data.
 * Decode the lines and quadratic beziers and makes all implicit points explicit.
 */
static void ttf_glyph_build(
    const u16*            contourEndpoints,
    const usize           numContours,
    const u8*             pointFlags,
    const AssetFontPoint* points,
    const usize           numPoints,
    DynArray*             outPoints,   // AssetFontPoint[], needs to be already initialized.
    DynArray*             outSegments, // AssetFontSegment[], needs to be already initialized.
    AssetFontGlyph*       outGlyph,
    TtfError*             err) {

  outGlyph->segmentIndex = (u32)outSegments->size;
  outGlyph->segmentCount = 0;

  for (usize c = 0; c != numContours; ++c) {
    const usize start = c ? contourEndpoints[c - 1U] : 0;
    const usize end   = contourEndpoints[c];
    if (UNLIKELY((end - start) < 2)) {
      // Not enough points in this contour to form a segment.
      // TODO: Investigate how we should handle this, it does happen with fonts in the wild.
      continue;
    }
    if (UNLIKELY(start > end)) {
      *err = TtfError_GlyfTableEntryContourMalformed;
      return;
    }
    if (UNLIKELY(end > numPoints)) {
      *err = TtfError_GlyfTableEntryContourMalformed;
      return;
    }

    *dynarray_push_t(outPoints, AssetFontPoint) = points[start];

    for (usize cur = start; cur != end; ++cur) {
      const bool  isLast      = (cur + 1) == end;
      const usize next        = isLast ? start : cur + 1; // Wraps around for the last entry.
      const bool  curOnCurve  = (pointFlags[cur] & TtfGlyphFlags_OnCurvePoint) != 0;
      const bool  nextOnCurve = (pointFlags[next] & TtfGlyphFlags_OnCurvePoint) != 0;

      if (nextOnCurve) {
        /**
         * Next is a point on the curve.
         * If the current is also on the curve then there is a straight line between them.
         * Otherwise this point 'finishes' the previous curve.
         */
        if (curOnCurve) {
          *dynarray_push_t(outSegments, AssetFontSegment) = (AssetFontSegment){
              .type       = AssetFontSegment_Line,
              .pointIndex = (u32)outPoints->size - 1,
          };
          ++outGlyph->segmentCount;
        }
      } else {
        /**
         * Next is a control point.
         * If the current is also a control point we synthesize the implicit 'on curve' point to
         * finish the previous curve.
         */
        if (!curOnCurve) {
          *dynarray_push_t(outPoints, AssetFontPoint) = (AssetFontPoint){
              .x = (points[cur].x + points[next].x) * 0.5f,
              .y = (points[cur].y + points[next].y) * 0.5f,
          };
        }
        *dynarray_push_t(outSegments, AssetFontSegment) = (AssetFontSegment){
            .type       = AssetFontSegment_QuadraticBezier,
            .pointIndex = (u32)outPoints->size - 1,
        };
        ++outGlyph->segmentCount;

        if (UNLIKELY(isLast)) {
          // Another point has to follow this one to finish the curve.
          *err = TtfError_GlyfTableEntryContourMalformed;
          return;
        }
      }

      *dynarray_push_t(outPoints, AssetFontPoint) = points[next];
    }
  }
  *err = TtfError_None;
}

static void ttf_read_glyph(
    Mem                       data,
    const TtfGlyphHorMetrics* horMetrics,
    const TtfHeadTable*       headTable,
    const usize               glyphId,
    DynArray*                 outPoints,   // AssetFontPoint[], needs to be already initialized.
    DynArray*                 outSegments, // AssetFontSegment[], needs to be already initialized.
    AssetFontGlyph*           outGlyph,
    TtfError*                 err) {

  *err      = TtfError_None;
  *outGlyph = (AssetFontGlyph){
      .advance = horMetrics->advanceWidth * headTable->invUnitsPerEm,
  };
  if (UNLIKELY(!data.size)) {
    return; // Glyphs without data are valid, for example a space character glyph.
  }

  TtfGlyphHeader header;
  data = ttf_read_glyph_header(data, headTable, &header, err);
  if (UNLIKELY(*err)) {
    return;
  }
  outGlyph->size    = header.size;
  outGlyph->offsetX = header.offsetX;
  outGlyph->offsetY = header.offsetY;

  if (UNLIKELY(header.numContours == 0)) {
    return;
  }
  if (UNLIKELY(header.numContours < 0)) {
    log_w(
        "Skipping unsupported ttf glyph",
        log_param("id", fmt_int(glyphId)),
        log_param("reason", fmt_text_lit("Composite glyphs are unsupported")));
    *outGlyph = (AssetFontGlyph){.segmentCount = 0};
    *err      = TtfError_None;
    return;
  }
  if (UNLIKELY(header.numContours > ttf_max_contours_per_glyph)) {
    *err = TtfError_TooManyContours;
    return;
  }

  // Read contour data.
  if (UNLIKELY(data.size < (usize)header.numContours * 2)) {
    *err = TtfError_GlyfTableEntryMalformed;
    return;
  }
  u16* contourEndpoints = mem_stack(sizeof(u16) * header.numContours).ptr;
  for (usize i = 0; i != (usize)header.numContours; ++i) {
    data = mem_consume_be_u16(data, &contourEndpoints[i]);
    ++contourEndpoints[i]; // +1 because 'end' meaning one past the last is more idiomatic.
  }

  // Skip over ttf instruction byte code for hinting, we do not support it.
  if (UNLIKELY(data.size < 2)) {
    *err = TtfError_GlyfTableEntryMalformed;
    return;
  }
  u16 instructionsLength;
  data = mem_consume_be_u16(data, &instructionsLength);
  if (UNLIKELY(data.size < instructionsLength)) {
    *err = TtfError_GlyfTableEntryMalformed;
    return;
  }
  data = mem_consume(data, instructionsLength);

  // Lookup the amount of points in this glyph.
  const usize numPoints = contourEndpoints[header.numContours - 1];
  if (UNLIKELY(numPoints > ttf_max_points_per_glyph)) {
    *err = TtfError_TooManyPoints;
    return;
  }

  // Read flags.
  u8* flags = mem_stack(numPoints).ptr;
  data      = ttf_read_glyph_flags(data, numPoints, flags, err);
  if (UNLIKELY(*err)) {
    return;
  }

  // Read points.
  AssetFontPoint* points = mem_stack(sizeof(AssetFontPoint) * numPoints).ptr;
  data                   = ttf_read_glyph_points(data, &header, flags, numPoints, points, err);
  if (UNLIKELY(*err)) {
    return;
  }

  // Output the glyph.
  ttf_glyph_build(
      contourEndpoints,
      header.numContours,
      flags,
      points,
      numPoints,
      outPoints,
      outSegments,
      outGlyph,
      err);
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

static void ttf_validate(const TtfOffsetTable* offsetTable, TtfError* err) {
  for (usize i = 0; i != offsetTable->numTables; ++i) {
    const TtfTableRecord* record = &offsetTable->records[i];
    if (string_eq(record->tag, string_lit("head"))) {
      // TODO: Validate head table checksum, for the head table the checksum works differently as
      // it contains a checksum adjustment for the entire font.
      continue;
    }
    if (ttf_checksum(record->data) != record->checksum) {
      *err = TtfError_TableChecksumFailed;
      return;
    }
  }
  *err = TtfError_None;
}

static void ttf_load_succeed(
    EcsWorld*         world,
    const EcsEntityId entity,
    const DynArray*   characters, // AssetFontChar[]
    const DynArray*   points,     // AssetFontPoint[]
    const DynArray*   segments,   // AssetFontSegment[]
    AssetFontGlyph*   glyphs,     // Moved into the result component which will take ownership.
    const usize       glyphCount) {
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  AssetFontComp* result = ecs_world_add_t(world, entity, AssetFontComp);

  // Copy the characters to the component.
  result->characters     = dynarray_copy_as_new(characters, g_alloc_heap);
  result->characterCount = characters->size;

  // Copy the points to the component.
  result->points     = dynarray_copy_as_new(points, g_alloc_heap);
  result->pointCount = points->size;

  // Copy the segments to the component.
  result->segments     = dynarray_copy_as_new(segments, g_alloc_heap);
  result->segmentCount = segments->size;

  // Move the glyphs to the component.
  result->glyphs     = glyphs;
  result->glyphCount = glyphCount;
}

static void ttf_load_fail(EcsWorld* world, const EcsEntityId entity, const TtfError err) {
  log_e("Failed to parse TrueType font", log_param("error", fmt_text(ttf_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

void asset_load_ttf(EcsWorld* world, const EcsEntityId entity, AssetSource* src) {
  TtfError            err                = TtfError_None;
  DynArray            characters         = dynarray_create_t(g_alloc_heap, AssetFontChar, 128);
  DynArray            points             = dynarray_create_t(g_alloc_heap, AssetFontPoint, 1024);
  DynArray            segments           = dynarray_create_t(g_alloc_heap, AssetFontSegment, 512);
  Mem*                glyphDataLocations = null; // Mem[maxpTable.numGlyphs]
  TtfGlyphHorMetrics* glyphHorMetrics    = null; // TtfGlyphHorMetrics[maxpTable.numGlyphs]
  AssetFontGlyph*     glyphs             = null; // AssetFontGlyph[maxpTable.numGlyphs]

  TtfOffsetTable offsetTable;
  ttf_read_offset_table(src->data, &offsetTable, &err);
  if (err) {
    ttf_load_fail(world, entity, err);
    goto End;
  }
  if (offsetTable.sfntVersion != ttf_supported_sfnt_version) {
    ttf_load_fail(world, entity, TtfError_UnsupportedSfntVersion);
    goto End;
  }
  ttf_validate(&offsetTable, &err);
  if (err) {
    ttf_load_fail(world, entity, err);
    goto End;
  }

  TtfHeadTable headTable;
  ttf_read_head_table(&offsetTable, &headTable, &err);
  if (err) {
    ttf_load_fail(world, entity, err);
    goto End;
  }
  if (headTable.magicNumber != ttf_magic) {
    ttf_load_fail(world, entity, TtfError_HeadTableMalformed);
    goto End;
  }
  if (headTable.majorVersion != 0 && headTable.majorVersion != 1) {
    ttf_load_fail(world, entity, TtfError_HeadTableUnsupported);
    goto End;
  }

  TtfMaxpTable maxpTable;
  ttf_read_maxp_table(&offsetTable, &maxpTable, &err);
  if (err) {
    ttf_load_fail(world, entity, err);
    goto End;
  }

  TtfCmapTable cmapTable;
  ttf_read_cmap_table(&offsetTable, &cmapTable, &err);
  if (err) {
    ttf_load_fail(world, entity, err);
    goto End;
  }
  ttf_read_characters(&cmapTable, &maxpTable, &characters, &err);
  if (err) {
    ttf_load_fail(world, entity, err);
    goto End;
  }
  if (!characters.size) {
    ttf_load_fail(world, entity, TtfError_NoCharacters);
    goto End;
  }
  dynarray_sort(&characters, asset_font_compare_char); // Sort on the unicode codepoint.

  TtfHheaTable hheaTable;
  ttf_read_hhea_table(&offsetTable, &hheaTable, &err);
  if (err) {
    ttf_load_fail(world, entity, err);
    goto End;
  }

  if (maxpTable.numGlyphs > ttf_max_glyphs) {
    ttf_load_fail(world, entity, TtfError_TooManyGlyphs);
    goto End;
  }

  glyphDataLocations = alloc_array_t(g_alloc_heap, Mem, maxpTable.numGlyphs);
  ttf_read_glyph_locations(&offsetTable, &maxpTable, &headTable, glyphDataLocations, &err);
  if (err) {
    ttf_load_fail(world, entity, err);
    goto End;
  }

  glyphHorMetrics = alloc_array_t(g_alloc_heap, TtfGlyphHorMetrics, maxpTable.numGlyphs);
  ttf_read_glyph_hor_metrics(&offsetTable, &maxpTable, &hheaTable, glyphHorMetrics, &err);
  if (err) {
    ttf_load_fail(world, entity, err);
    goto End;
  }

  glyphs = alloc_array_t(g_alloc_heap, AssetFontGlyph, maxpTable.numGlyphs);
  for (usize glyphIndex = 0; glyphIndex != maxpTable.numGlyphs; ++glyphIndex) {
    ttf_read_glyph(
        glyphDataLocations[glyphIndex],
        &glyphHorMetrics[glyphIndex],
        &headTable,
        glyphIndex,
        &points,
        &segments,
        &glyphs[glyphIndex],
        &err);
    if (err) {
      ttf_load_fail(world, entity, err);
      goto End;
    }
  }
  if (!points.size) {
    ttf_load_fail(world, entity, TtfError_NoGlyphPoints);
    goto End;
  }
  if (!segments.size) {
    ttf_load_fail(world, entity, TtfError_NoGlyphSegments);
    goto End;
  }
  ttf_load_succeed(world, entity, &characters, &points, &segments, glyphs, maxpTable.numGlyphs);
  glyphs = null; // Moved into the result component, which will take ownership.

End:
  dynarray_destroy(&characters);
  dynarray_destroy(&points);
  dynarray_destroy(&segments);
  if (glyphDataLocations) {
    alloc_free_array_t(g_alloc_heap, glyphDataLocations, maxpTable.numGlyphs);
  }
  if (glyphHorMetrics) {
    alloc_free_array_t(g_alloc_heap, glyphHorMetrics, maxpTable.numGlyphs);
  }
  if (glyphs) {
    alloc_free_array_t(g_alloc_heap, glyphs, maxpTable.numGlyphs);
  }
  asset_repo_source_close(src);
}
