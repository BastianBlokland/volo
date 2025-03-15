#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_math.h"
#include "core_zlib.h"
#include "ecs_entity.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "import_texture_internal.h"
#include "loader_texture_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

/**
 * Portable Network Graphics.
 * NOTE: Only 8/16 bit images are supported.
 * NOTE: Interlaced images are not supported.
 * NOTE: Grayscale with alpha is not supported.
 *
 * Spec: https://www.w3.org/TR/png-3/
 */

#define png_max_chunks 2048
#define png_max_width (1024 * 16)
#define png_max_height (1024 * 16)

static const String g_pngMagic = string_static("\x89\x50\x4E\x47\x0D\x0A\x1A\x0A");

typedef struct {
  u8  type[4];
  Mem data;
} PngChunk;

typedef enum {
  PngChannels_Invalid,
  PngChannels_R    = 1,
  PngChannels_RG   = 2, // NOTE: Png specifies this as RA but we import it as RG.
  PngChannels_RGB  = 3,
  PngChannels_RGBA = 4,
} PngChannels;

typedef enum {
  PngType_Invalid,
  PngType_u8  = 1,
  PngType_u16 = 2,
} PngType;

typedef enum {
  PngFilterType_None,
  PngFilterType_Sub,
  PngFilterType_Up,
  PngFilterType_Average,
  PngFilterType_Paeth,
} PngFilterType;

typedef struct {
  u32 width, height;
  u8  bitDepth;
  u8  colorType;
  u8  compressionMethod, filterMethod, interlaceMethod;
} PngHeader;

typedef enum {
  PngError_None = 0,
  PngError_MagicMismatch,
  PngError_Truncated,
  PngError_Malformed,
  PngError_ChunkLimitExceeded,
  PngError_ChunkChecksumFailed,
  PngError_HeaderChunkMissing,
  PngError_PaletteChunkMissing,
  PngError_PaletteChunkInvalid,
  PngError_EndChunkMissing,
  PngError_InvalidIndexBitDepth,
  PngError_DataMissing,
  PngError_DataMalformed,
  PngError_DataUnexpectedSize,
  PngError_DataInvalidFilter,
  PngError_UnsupportedColorType,
  PngError_UnsupportedCompression,
  PngError_UnsupportedFilter,
  PngError_UnsupportedInterlacing,
  PngError_UnsupportedBitDepth,
  PngError_UnsupportedSize,
  PngError_ImportFailed,

  PngError_Count,
} PngError;

static String png_error_str(const PngError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Data is not a png file"),
      string_static("Truncated png data"),
      string_static("Malformed png data"),
      string_static("Png exceeds chunk limit"),
      string_static("Png chunk checksum failed"),
      string_static("Png header chunk missing"),
      string_static("Png palette chunk missing"),
      string_static("Png palette chunk invalid"),
      string_static("Png end chunk missing"),
      string_static("Png invalid index bit-depth"),
      string_static("Png data missing"),
      string_static("Png data malformed"),
      string_static("Png unexpected data size"),
      string_static("Png data filter invalid"),
      string_static("Unsupported png color-type (only R, RGB, and RGBA supported)"),
      string_static("Unsupported png compression method"),
      string_static("Unsupported png filter method"),
      string_static("Unsupported png interlace method (only non-interlaced is supported)"),
      string_static("Unsupported image bit depth (only 8/16 bit are supported)"),
      string_static("Unsupported image size"),
      string_static("Import failed"),
  };
  ASSERT(array_elems(g_msgs) == PngError_Count, "Incorrect number of png-error messages");
  return g_msgs[err];
}

static bool png_chunk_match(const PngChunk* chunk, const String type) {
  return mem_eq(array_mem(chunk->type), type);
}

static const PngChunk* png_chunk_find(const PngChunk chunks[], const u32 count, const String type) {
  for (u32 i = 0; i != count; ++i) {
    if (png_chunk_match(&chunks[i], type)) {
      return &chunks[i];
    }
  }
  return null;
}

static u32 png_read_chunks(Mem d, PngChunk out[PARAM_ARRAY_SIZE(png_max_chunks)], PngError* err) {
  // Read magic bytes.
  if (UNLIKELY(!string_starts_with(d, g_pngMagic))) {
    *err = PngError_MagicMismatch;
    return 0;
  }
  d = mem_consume(d, g_pngMagic.size);

  // Read all chunks.
  u32 chunkCount = 0;
  while (!string_is_empty(d)) {
    if (UNLIKELY(chunkCount == png_max_chunks)) {
      *err = PngError_ChunkLimitExceeded;
      return 0;
    }
    if (UNLIKELY(d.size < 4)) {
      *err = PngError_Truncated;
      return 0;
    }
    PngChunk* chunk = &out[chunkCount++];

    // Read length.
    u32 length;
    d = mem_consume_be_u32(d, &length);

    if (UNLIKELY(d.size < (length + 8))) {
      *err = PngError_Truncated;
      return 0;
    }
    const Mem typeAndDataMem = mem_slice(d, 0, length + 4);

    // Read type.
    mem_cpy(array_mem(chunk->type), mem_slice(d, 0, 4));
    d = mem_consume(d, sizeof(chunk->type));

    // Read data.
    chunk->data = mem_slice(d, 0, length);
    d           = mem_consume(d, length);

    // Read checksum.
    u32 crc;
    d = mem_consume_be_u32(d, &crc);

    // Validate checksum.
    if (UNLIKELY(crc != bits_crc_32(0, typeAndDataMem))) {
      *err = PngError_ChunkChecksumFailed;
      return 0;
    }
  }
  return chunkCount;
}

static void png_read_header(const PngChunk* chunk, PngHeader* out, PngError* err) {
  Mem d = chunk->data;
  if (UNLIKELY(d.size != 13)) {
    *err = PngError_Malformed;
    return;
  }
  d = mem_consume_be_u32(d, &out->width);
  d = mem_consume_be_u32(d, &out->height);
  d = mem_consume_u8(d, &out->bitDepth);
  d = mem_consume_u8(d, &out->colorType);
  d = mem_consume_u8(d, &out->compressionMethod);
  d = mem_consume_u8(d, &out->filterMethod);
  d = mem_consume_u8(d, &out->interlaceMethod);
}

static void png_read_data(const PngChunk chunks[], const u32 count, DynString* out, PngError* err) {
  u32   dataChunkCount = 0;
  usize dataChunkSize  = 0;
  for (u32 i = 0; i != count; ++i) {
    if (png_chunk_match(&chunks[i], string_lit("IDAT"))) {
      dataChunkCount += 1;
      dataChunkSize += chunks[i].data.size;
    }
  }

  if (UNLIKELY(!dataChunkSize)) {
    *err = PngError_DataMissing;
    return;
  }

  /**
   * The PNG spec allows splitting the ZLib stream across multiple IDAT chunks, because we only
   * support contigious zlib data we have to combine the chunks before decoding.
   */

  Mem   dataCombined = dataChunkCount ? alloc_alloc(g_allocHeap, dataChunkSize, 1) : mem_empty;
  usize dataOffset   = 0;
  for (u32 i = 0; i != count; ++i) {
    if (png_chunk_match(&chunks[i], string_lit("IDAT"))) {
      if (dataChunkCount) {
        mem_cpy(mem_consume(dataCombined, dataOffset), chunks[i].data);
      } else {
        dataCombined = chunks[i].data;
      }
      dataOffset += chunks[i].data.size;
    }
  }
  diag_assert(dataOffset = dataChunkSize);

  ZlibError zlibErr;
  zlib_decode(dataCombined, out, &zlibErr);
  if (UNLIKELY(zlibErr)) {
    *err = PngError_DataMalformed;
  }

  if (dataChunkCount) {
    alloc_free(g_allocHeap, dataCombined);
  }
}

/**
 * PaethPredictor function.
 * Based on the spec: https://www.w3.org/TR/png-3/#9Filter-type-4-Paeth
 */
static u8 png_paeth_predictor(const u8 a, const u8 b, const u8 c) {
  const i32 p  = (i32)a + (i32)b - (i32)c;
  const i32 pA = math_abs(p - a);
  const i32 pB = math_abs(p - b);
  const i32 pC = math_abs(p - c);
  if (pA <= pB && pA <= pC) {
    return a;
  }
  if (pB <= pC) {
    return b;
  }
  return c;
}

static void png_filter_decode(
    const PngHeader* header,
    const u32        sampleBytes,
    const u32        scanlineBytes,
    DynString*       data,
    PngError*        err) {

  const Mem dataMem            = dynstring_view(data);
  const u32 scanlineInputBytes = scanlineBytes + 1; // + 1 byte for filterType.

  /**
   * Inplace decode the filters for each scanline.
   *
   * NOTE: What the spec calls pixels are here called samples, reason is for indexed images they are
   * not actually pixels but instead indices.
   */
  for (u32 y = 0; y != header->height; ++y) {
    Mem scanlineMem = mem_slice(dataMem, scanlineInputBytes * y, scanlineInputBytes);

    // Read filter.
    u8 filterType;
    scanlineMem = mem_consume_u8(scanlineMem, &filterType);

    // Decode filter.
    switch (filterType) {
    case PngFilterType_None: // Recon(x) = Filt(x).
      break;
    case PngFilterType_Sub: // Recon(x) = Filt(x) + Recon(a).
      for (u32 i = sampleBytes; i != scanlineBytes; ++i) {
        // NOTE: Skip the first sample as 'a' is always zero.
        const u8 a = *mem_at_u8(scanlineMem, i - sampleBytes);
        *mem_at_u8(scanlineMem, i) += a;
      }
      break;
    case PngFilterType_Up: // Recon(x) = Filt(x) + Recon(b)
      if (y != 0) {
        // NOTE: Skip the first scanline as 'b' is always zero.
        for (u32 i = 0; i != scanlineBytes; ++i) {
          const u8 b = *mem_at_u8(dataMem, scanlineBytes * (y - 1) + i);
          *mem_at_u8(scanlineMem, i) += b;
        }
      }
      break;
    case PngFilterType_Average: // Recon(x) = Filt(x) + floor((Recon(a) + Recon(b)) / 2)
      if (y == 0) {
        for (u32 i = sampleBytes; i != scanlineBytes; ++i) {
          // First scanline: 'b' is always zero.
          const u8 a = *mem_at_u8(scanlineMem, i - sampleBytes);
          *mem_at_u8(scanlineMem, i) += a / 2;
        }
      } else /* y > 0 */ {
        for (u32 i = 0; i != sampleBytes; ++i) {
          // First sample: 'a' is always zero.
          const u8 b = *mem_at_u8(dataMem, scanlineBytes * (y - 1) + i);
          *mem_at_u8(scanlineMem, i) += b / 2;
        }
        for (u32 i = sampleBytes; i != scanlineBytes; ++i) {
          const u32 a = *mem_at_u8(scanlineMem, i - sampleBytes);
          const u32 b = *mem_at_u8(dataMem, scanlineBytes * (y - 1) + i);
          *mem_at_u8(scanlineMem, i) += (a + b) / 2;
        }
      }
      break;
    case PngFilterType_Paeth: // Recon(x) = Filt(x) + PaethPredictor(Recon(a), Recon(b), Recon(c))
      if (y == 0) {
        // First scanline: 'b' and 'c' are always zero.
        // NOTE: Skip the first scanline as 'a' is always zero there as well.
        for (u32 i = sampleBytes; i != scanlineBytes; ++i) {
          const u8 a = *mem_at_u8(scanlineMem, i - sampleBytes);
          *mem_at_u8(scanlineMem, i) += png_paeth_predictor(a, 0, 0);
        }
      } else /* y > 0 */ {
        for (u32 i = 0; i != sampleBytes; ++i) {
          // First sample: 'a' and 'c' are always zero.
          const u8 b = *mem_at_u8(dataMem, scanlineBytes * (y - 1) + i);
          *mem_at_u8(scanlineMem, i) += png_paeth_predictor(0, b, 0);
        }
        for (u32 i = sampleBytes; i != scanlineBytes; ++i) {
          const u8 a = *mem_at_u8(scanlineMem, i - sampleBytes);
          const u8 b = *mem_at_u8(dataMem, scanlineBytes * (y - 1) + i);
          const u8 c = *mem_at_u8(dataMem, scanlineBytes * (y - 1) + i - sampleBytes);
          *mem_at_u8(scanlineMem, i) += png_paeth_predictor(a, b, c);
        }
      }
      break;
    default:
      *err = PngError_DataInvalidFilter;
      return;
    }

    // Move the scanline into its final position (removing the filterType bytes).
    mem_move(mem_slice(dataMem, scanlineBytes * y, scanlineBytes), scanlineMem);
  }

  dynstring_resize(data, scanlineBytes * header->height);
}

static void png_palette_decode(
    const PngHeader* header,
    const PngChunk   chunks[],
    const u32        chunkCount,
    DynString*       data,
    PngError*        err) {
  const PngChunk* paletteChunk = png_chunk_find(chunks, chunkCount, string_lit("PLTE"));
  const PngChunk* transChunk   = png_chunk_find(chunks, chunkCount, string_lit("tRNS"));
  if (UNLIKELY(!paletteChunk)) {
    *err = PngError_PaletteChunkMissing;
    return;
  }
  if (UNLIKELY(!bits_aligned(paletteChunk->data.size, 3))) {
    *err = PngError_PaletteChunkInvalid;
    return;
  }
  const u32 paletteEntries = (u32)paletteChunk->data.size / 3;
  const u8* paletteData    = paletteChunk->data.ptr;

  const u32 transEntries = transChunk ? (u32)transChunk->data.size : 0;
  const u8* transData    = transChunk ? transChunk->data.ptr : null;

  const u32 rowSize   = header->width * (transChunk ? 4 /* rgba */ : 3 /* rgb */);
  const Mem rowBuffer = alloc_alloc(g_allocScratch, rowSize, 1);

  const usize scanlineBytes = math_max(1, bits_to_bytes(header->width * header->bitDepth));

  u32 inputStep = 8 / header->bitDepth;
  for (u32 y = 0; y != header->height; ++y) {
    const u8* inputItr = mem_at_u8(dynstring_view(data), y * rowSize);
    u8        inputByte;

    u8* outputItr = mem_begin(rowBuffer);
    for (u32 x = 0; x != header->width; ++x) {
      // Read an input sample.
      if (!(x % inputStep)) {
        inputByte = *inputItr++;
      }
      const u8 index = inputByte >> (8 - header->bitDepth);
      inputByte <<= header->bitDepth;

      // Validate the index.
      if (UNLIKELY(index >= paletteEntries)) {
        *err = PngError_PaletteChunkInvalid;
        return;
      }

      // Output the corresponding palette color.
      *outputItr++ = paletteData[index * 3 + 0];
      *outputItr++ = paletteData[index * 3 + 1];
      *outputItr++ = paletteData[index * 3 + 2];
      if (transChunk) {
        *outputItr++ = index >= transEntries ? 255 : transData[index];
      }
    }

    // Output the row.
    dynarray_insert(data, y * rowSize, rowSize - scanlineBytes);
    mem_cpy(mem_slice(dynstring_view(data), y * rowSize, rowSize), rowBuffer);
  }
}

static bool png_is_linear(const PngChunk chunks[], const u32 chunkCount) {
  /**
   * Most png images found in the wild are sRGB encoded (or atleast non-linear) often without any
   * color profile data in the png file at all. Therefore we only treat textures as linear as they
   * explicitly specify a gamma of 1.0.
   */
  const PngChunk* srgbChunk = png_chunk_find(chunks, chunkCount, string_lit("sRGB"));
  if (srgbChunk) {
    return false; // Texture is explicitly sRGB encoded.
  }
  const PngChunk* gammaChunk = png_chunk_find(chunks, chunkCount, string_lit("gAMA"));
  if (gammaChunk && gammaChunk->data.size == 4) {
    u32 gammaVal; // gamma * 100000.
    mem_consume_be_u32(gammaChunk->data, &gammaVal);
    return gammaVal == 100000; // Gamma 1.0 therefore linear.
  }
  return false; // Gamma unknown, assume sRGB.
}

static PngType png_type(const PngHeader* header) {
  switch (header->bitDepth) {
  case 8:
    return PngType_u8;
  case 16:
    return PngType_u16;
  default:
    return PngType_Invalid;
  }
}

static PngChannels png_channels(const PngHeader* header) {
  switch (header->colorType) {
  case 0:
    return PngChannels_R;
  case 2:
    return PngChannels_RGB;
  case 4:
    /**
     * NOTE: Png specifies this as RA (single channel + alpha), unfortunately this is not something
     * we support so we import it as RG (red + green).
     */
    return PngChannels_RG;
  case 6:
    return PngChannels_RGBA;
  default:
    return PngChannels_Invalid;
  }
}

static void png_load_fail(EcsWorld* w, const EcsEntityId e, const String id, const PngError err) {
  log_e(
      "Failed to parse Png texture",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(e)),
      log_param("error", fmt_text(png_error_str(err))));
  ecs_world_add_empty_t(w, e, AssetFailedComp);
}

static AssetTextureType png_tex_type(const PngType type) {
  switch (type) {
  case PngType_u8:
    return AssetTextureType_u8;
  case PngType_u16:
    return AssetTextureType_u16;
  default:
    diag_crash();
  }
}

void asset_load_tex_png(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {

  DynString buffer = dynstring_create(g_allocHeap, 0);

  PngError  err = PngError_None;
  PngChunk  chunks[png_max_chunks];
  const u32 chunkCount = png_read_chunks(src->data, chunks, &err);
  if (UNLIKELY(err)) {
    png_load_fail(world, entity, id, err);
    goto Ret;
  }
  if (UNLIKELY(!chunkCount || !png_chunk_match(&chunks[0], string_lit("IHDR")))) {
    png_load_fail(world, entity, id, PngError_HeaderChunkMissing);
    goto Ret;
  }
  if (UNLIKELY(!chunkCount || !png_chunk_match(&chunks[chunkCount - 1], string_lit("IEND")))) {
    png_load_fail(world, entity, id, PngError_EndChunkMissing);
    goto Ret;
  }

  PngHeader header;
  png_read_header(&chunks[0], &header, &err);
  if (UNLIKELY(err)) {
    png_load_fail(world, entity, id, err);
    goto Ret;
  }
  PngType     type;
  PngChannels channels;
  u32         indexBits;
  if (header.colorType == 3 /* indexed color */) {
    type = PngType_u8;
    if (png_chunk_find(chunks, chunkCount, string_lit("tRNS"))) {
      channels = PngChannels_RGBA;
    } else {
      channels = PngChannels_RGB;
    }
    indexBits = header.bitDepth;
  } else {
    type      = png_type(&header);
    channels  = png_channels(&header);
    indexBits = 0; // Non-indexed direct pixel values.
  }
  if (UNLIKELY(!type)) {
    png_load_fail(world, entity, id, PngError_UnsupportedBitDepth);
    goto Ret;
  }
  if (UNLIKELY(!channels)) {
    png_load_fail(world, entity, id, PngError_UnsupportedColorType);
    goto Ret;
  }
  if (UNLIKELY((indexBits > 1) && (!bits_ispow2(indexBits) || indexBits > 8))) {
    png_load_fail(world, entity, id, PngError_InvalidIndexBitDepth);
  }
  if (UNLIKELY(!header.width || !header.height)) {
    png_load_fail(world, entity, id, PngError_UnsupportedSize);
    goto Ret;
  }
  if (UNLIKELY(header.width > png_max_width || header.height > png_max_height)) {
    png_load_fail(world, entity, id, PngError_UnsupportedSize);
    goto Ret;
  }
  if (UNLIKELY(header.compressionMethod)) {
    png_load_fail(world, entity, id, PngError_UnsupportedCompression);
    goto Ret;
  }
  if (UNLIKELY(header.filterMethod)) {
    png_load_fail(world, entity, id, PngError_UnsupportedFilter);
    goto Ret;
  }
  if (UNLIKELY(header.interlaceMethod)) {
    png_load_fail(world, entity, id, PngError_UnsupportedInterlacing);
    goto Ret;
  }

  /**
   * NOTE: For indexed images a sample refers to an index into the palette for other images types it
   * refers to an actual pixel.
   */
  const u32   sampleBits          = indexBits ? indexBits : bytes_to_bits((u32)channels * type);
  const u32   sampleBytes         = math_max(1, bits_to_bytes(sampleBits));
  const u32   sampleScanlineBytes = math_max(1, bits_to_bytes(header.width * sampleBits));
  const usize sampleTotalBytes    = header.height * sampleScanlineBytes;

  const usize filterTotalBytes = header.height * sizeof(u8);
  const usize inputTotalBytes  = sampleTotalBytes + filterTotalBytes;
  const usize pixelTotalBytes  = header.width * header.height * channels * type;

  dynstring_reserve(&buffer, math_max(inputTotalBytes, pixelTotalBytes));

  png_read_data(chunks, chunkCount, &buffer, &err);
  if (UNLIKELY(err)) {
    png_load_fail(world, entity, id, err);
    goto Ret;
  }

  if (UNLIKELY(buffer.size != inputTotalBytes)) {
    png_load_fail(world, entity, id, PngError_DataUnexpectedSize);
    goto Ret;
  }

  png_filter_decode(&header, sampleBytes, sampleScanlineBytes, &buffer, &err);
  if (UNLIKELY(err)) {
    png_load_fail(world, entity, id, err);
    goto Ret;
  }
  diag_assert(buffer.size == sampleTotalBytes);

  if (indexBits) {
    png_palette_decode(&header, chunks, chunkCount, &buffer, &err);
    if (UNLIKELY(err)) {
      png_load_fail(world, entity, id, err);
      goto Ret;
    }
  }
  diag_assert(buffer.size == pixelTotalBytes);

  AssetImportTextureFlags importFlags = AssetImportTextureFlags_Mips;
  if (png_is_linear(chunks, chunkCount)) {
    importFlags |= AssetImportTextureFlags_Linear;
  }

  AssetImportTextureFlip importFlip = 0;
  /**
   * Png defines y0 as the top-left and we are using y0 as bottom-left so we need to flip.
   */
  importFlip |= AssetImportTextureFlip_Y;

  AssetTextureComp tex;
  if (!asset_import_texture(
          importEnv,
          id,
          dynstring_view(&buffer),
          header.width,
          header.height,
          channels,
          png_tex_type(type),
          importFlags,
          importFlip,
          &tex)) {
    png_load_fail(world, entity, id, PngError_ImportFailed);
    goto Ret;
  }

  *ecs_world_add_t(world, entity, AssetTextureComp) = tex;
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  asset_cache(world, entity, g_assetTexMeta, mem_var(tex));

Ret:
  dynarray_destroy(&buffer);
  asset_repo_source_close(src);
}
