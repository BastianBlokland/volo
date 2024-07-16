#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "loader_texture_internal.h"
#include "repo_internal.h"

/**
 * Truevision TGA.
 * Supports 8 bit (r), 24 bit (rgb) and 32 bit (rgba) and optionally rle compressed.
 * Format information: https://en.wikipedia.org/wiki/Truevision_TGA
 * Format examples: http://www.gamers.org/dEngine/quake3/TGA.txt
 * Color info: http://www.ryanjuckett.com/programming/parsing-colors-in-a-tga-file/
 */

#define tga_max_width (1024 * 16)
#define tga_max_height (1024 * 16)

typedef enum {
  TgaColorMapType_Absent  = 0,
  TgaColorMapType_Present = 1,
} TgaColorMapType;

typedef enum {
  TgaImageType_ColorMapped    = 1,
  TgaImageType_TrueColor      = 2,
  TgaImageType_Grayscale      = 3,
  TgaImageType_RleColorMapped = 9,
  TgaImageType_RleTrueColor   = 10,
  TgaImageType_RleGrayscale   = 11,
} TgaImageType;

typedef enum {
  TgaOrigin_LowerLeft  = 0,
  TgaOrigin_LowerRight = 1,
  TgaOrigin_UpperLeft  = 2,
  TgaOrigin_UpperRight = 3,
} TgaOrigin;

typedef enum {
  TgaInterleave_None    = 0,
  TgaInterleave_EvenOdd = 1,
  TgaInterleave_FourWay = 2,
} TgaInterleave;

typedef struct {
  u16 mapStart, mapLength;
  u8  entrySize;
} TgaColorMapSpec;

typedef struct {
  u8            attributeDepth;
  TgaOrigin     origin;
  TgaInterleave interleave;
} TgaImageDescriptor;

typedef struct {
  u16                origin[2];
  u16                width, height;
  u8                 bitsPerPixel;
  TgaImageDescriptor descriptor;
} TgaImageSpec;

typedef struct {
  u8              idLength;
  TgaColorMapType colorMapType;
  TgaImageType    imageType;
  TgaColorMapSpec colorMapSpec;
  TgaImageSpec    imageSpec;
} TgaHeader;

typedef enum {
  TgaChannels_Invalid,
  TgaChannels_R    = 1,
  TgaChannels_RGB  = 3,
  TgaChannels_RGBA = 4,
} TgaChannels;

typedef enum {
  TgaFlags_Rle   = 1 << 0,
  TgaFlags_YFlip = 1 << 1,
} TgaFlags;

typedef enum {
  TgaError_None = 0,
  TgaError_MalformedHeader,
  TgaError_MalformedPixels,
  TgaError_MalformedRlePixels,
  TgaError_Malformed,
  TgaError_UnsupportedColorMap,
  TgaError_UnsupportedBitDepth,
  TgaError_UnsupportedAlphaChannelDepth,
  TgaError_UnsupportedInterleaved,
  TgaError_UnsupportedNonTrueColor,
  TgaError_UnsupportedSize,

  TgaError_Count,
} TgaError;

static String tga_error_str(const TgaError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Malformed tga header"),
      string_static("Malformed tga pixel data"),
      string_static("Malformed Run-length-encoded tga pixel data"),
      string_static("Tga data is malformed"),
      string_static("Color-mapped Tga files are not supported"),
      string_static("Unsupported bit depth, 8 (R), 24 (RGB) and 32 (RGBA) are supported"),
      string_static("Only an 8 bit alpha channel is supported"),
      string_static("Interleaved tga files are not supported"),
      string_static("Unsupported image type, only TrueColor is supported"),
      string_static("Unsupported image size"),
  };
  ASSERT(array_elems(g_msgs) == TgaError_Count, "Incorrect number of tga-error messages");
  return g_msgs[err];
}

static Mem tga_read_header(Mem input, TgaHeader* out, TgaError* err) {
  if (UNLIKELY(input.size < 18)) {
    *err = TgaError_MalformedHeader;
    return input;
  }
  *out  = (TgaHeader){0};
  input = mem_consume_u8(input, &out->idLength);
  input = mem_consume_u8(input, (u8*)&out->colorMapType);
  input = mem_consume_u8(input, (u8*)&out->imageType);
  input = mem_consume_le_u16(input, &out->colorMapSpec.mapStart);
  input = mem_consume_le_u16(input, &out->colorMapSpec.mapLength);
  input = mem_consume_u8(input, &out->colorMapSpec.entrySize);
  input = mem_consume_le_u16(input, &out->imageSpec.origin[0]);
  input = mem_consume_le_u16(input, &out->imageSpec.origin[1]);
  input = mem_consume_le_u16(input, &out->imageSpec.width);
  input = mem_consume_le_u16(input, &out->imageSpec.height);
  input = mem_consume_u8(input, &out->imageSpec.bitsPerPixel);

  u8 imageSpecDescriptorRaw;
  input                     = mem_consume_u8(input, &imageSpecDescriptorRaw);
  out->imageSpec.descriptor = (TgaImageDescriptor){
      .attributeDepth = imageSpecDescriptorRaw & u8_lit(0b1111),
      .origin         = (TgaOrigin)((imageSpecDescriptorRaw & u8_lit(0b110000)) >> 4),
      .interleave     = (TgaInterleave)((imageSpecDescriptorRaw & u8_lit(0b11000000)) >> 6),
  };
  *err = TgaError_None;
  return input;
}

static TgaChannels tga_channels_from_bit_depth(const u32 bitDepth) {
  switch (bitDepth) {
  case 8:
    return TgaChannels_R;
  case 24:
    return TgaChannels_RGB;
  case 32:
    return TgaChannels_RGBA;
  default:
    return TgaChannels_Invalid;
  }
}

static bool tga_type_supported(const TgaImageType type) {
  switch (type) {
  case TgaImageType_TrueColor:
  case TgaImageType_Grayscale:
  case TgaImageType_RleTrueColor:
  case TgaImageType_RleGrayscale:
    return true;
  case TgaImageType_ColorMapped:
  case TgaImageType_RleColorMapped:
    break;
  }
  return false;
}

static void tga_mem_consume_inplace(Mem* mem, const usize amount) {
  mem->ptr = bits_ptr_offset(mem->ptr, amount);
  mem->size -= amount;
}

static u32 tga_index(const u32 x, const u32 y, const u32 width, const u32 height, TgaFlags flags) {
  // Either fill pixels from bottom to top - left to right, or top to bottom - left to right.
  return ((flags & TgaFlags_YFlip) ? (height - 1 - y) * width : y * width) + x;
}

static AssetTextureFormat tga_texture_format(const TgaChannels channels) {
  switch (channels) {
  case TgaChannels_R:
    return AssetTextureFormat_u8_r;
  case TgaChannels_RGB:
  case TgaChannels_RGBA:
    return AssetTextureFormat_u8_rgba;
  case TgaChannels_Invalid:
    break;
  }
  diag_crash_msg("Unsupported Tga channels value");
}

static AssetTextureFlags tga_texture_flags(const TgaChannels ch, const bool nrm, const bool alpha) {
  AssetTextureFlags flags = AssetTextureFlags_GenerateMipMaps;
  if (alpha) {
    flags |= AssetTextureFlags_Alpha;
  }
  if (nrm) {
    // Normal maps are in linear space (and thus not sRGB).
    flags |= AssetTextureFlags_NormalMap;
  } else if (ch == TgaChannels_RGB || ch == TgaChannels_RGBA) {
    // All other (3 or 4 channel) textures are assumed to be sRGB encoded.
    flags |= AssetTextureFlags_Srgb;
  }
  return flags;
}

static Mem tga_pixels_alloc(Allocator* alloc, const TgaChannels ch, const u32 w, const u32 h) {
  const AssetTextureFormat texFormat = tga_texture_format(ch);
  const u32                texLayers = 1;
  const u32                texMips   = 1;
  const usize              size      = asset_texture_req_size(texFormat, w, h, texLayers, texMips);
  const usize              align     = asset_texture_req_align(texFormat);
  return alloc_alloc(alloc, size, align);
}

static void tga_pixels_copy_at(
    const Mem         pixels /* u8[width * height] or u8[width * height * 4] */,
    const TgaChannels channels,
    const usize       dst,
    const usize       src) {
  switch (channels) {
  case TgaChannels_R: {
    ((u8*)pixels.ptr)[dst] = ((const u8*)pixels.ptr)[src];
  } break;
  case TgaChannels_RGB:
  case TgaChannels_RGBA: {
    ((u32*)pixels.ptr)[dst] = ((const u32*)pixels.ptr)[src];
  } break;
  default:
    UNREACHABLE
  }
}

static void tga_pixels_read_at(
    const Mem         pixels /* u8[width * height] or u8[width * height * 4] */,
    const TgaChannels channels,
    const u8*         data,
    const usize       index,
    bool*             outAlpha) {

  /**
   * Follows the same to RGBA conversion rules as the Vulkan spec:
   * https://registry.khronos.org/vulkan/specs/1.0/html/chap16.html#textures-conversion-to-rgba
   */

  switch (channels) {
  case TgaChannels_R:
    ((u8*)pixels.ptr)[index] = data[0];
    break;
  case TgaChannels_RGB:
    ((u32*)pixels.ptr)[index] = (data[2] << 0) | (data[1] << 8) | (data[0] << 16) | (255 << 24);
    break;
  case TgaChannels_RGBA:
    ((u32*)pixels.ptr)[index] = (data[2] << 0) | (data[1] << 8) | (data[0] << 16) | (data[3] << 24);
    if (data[3] != 255) {
      *outAlpha = true;
    }
    break;
  default:
    UNREACHABLE
  }
}

static Mem tga_pixels_read_uncompressed(
    const Mem         pixels /* AssetTexturePixelB1* or AssetTexturePixelB4* */,
    const TgaChannels channels,
    const TgaFlags    flags,
    const u32         width,
    const u32         height,
    const Mem         input,
    bool*             outAlpha,
    TgaError*         err) {

  const u32 pixelCount = width * height;

  if (input.size < pixelCount * channels) {
    *err = TgaError_MalformedPixels;
    return input;
  }
  u8* src = mem_begin(input);
  for (u32 y = 0; y != height; ++y) {
    for (u32 x = 0; x != width; ++x) {
      tga_pixels_read_at(pixels, channels, src, tga_index(x, y, width, height, flags), outAlpha);
      src += channels;
    }
  }
  *err = TgaError_None;
  return mem_consume(input, pixelCount * channels);
}

static Mem tga_pixels_read_rle(
    const Mem         pixels /* AssetTexturePixelB1* or AssetTexturePixelB4* */,
    const TgaChannels channels,
    const TgaFlags    flags,
    const u32         width,
    const u32         height,
    Mem               input,
    bool*             outAlpha,
    TgaError*         err) {

  u32 packetRem      = 0; // How many pixels are left in the current rle packet.
  u32 packetRefPixel = u32_max;
  for (u32 y = 0; y != height; ++y) {
    for (u32 x = 0; x != width; ++x) {
      const u32 i = tga_index(x, y, width, height, flags);

      /**
       * In run-length-encoding there is a header before each 'packet':
       * - run-length-packet: Contains a repetition count and a single pixel to repeat.
       * - raw-packet: Contains a count of how many 'raw' pixels will follow.
       */

      if (!packetRem) {
        /**
         * No pixels are remaining; Read a new packet header.
         */
        if (UNLIKELY(input.size <= (usize)channels)) {
          *err = TgaError_MalformedRlePixels;
          return input;
        }
        u8 packetHeader = *mem_begin(input);
        tga_mem_consume_inplace(&input, 1);
        const bool isRlePacket = (packetHeader & 0b10000000) != 0; // Msb indicates packet type.
        packetRefPixel         = isRlePacket ? i : u32_max;
        packetRem              = packetHeader & 0b01111111; // Remaining 7 bits are the rep count.

        if (UNLIKELY(!isRlePacket && input.size < (packetRem + 1) * channels)) {
          *err = TgaError_MalformedRlePixels;
          return input;
        }
      } else {
        // This pixel is still part of the same packet.
        --packetRem;
      }

      if (packetRefPixel < i) {
        // There is a reference pixel; Use that instead of reading a new one.
        tga_pixels_copy_at(pixels, channels, i, packetRefPixel);
      } else {
        // No reference pixel; Read a new pixel value.
        tga_pixels_read_at(pixels, channels, mem_begin(input), i, outAlpha);
        tga_mem_consume_inplace(&input, channels);
      }
    }
  }
  *err = TgaError_None;
  return input;
}

static Mem tga_pixels_read(
    const Mem         pixels /* AssetTexturePixelB1* or AssetTexturePixelB4* */,
    const TgaChannels channels,
    const TgaFlags    flags,
    const u32         width,
    const u32         height,
    const Mem         input,
    bool*             outAlpha,
    TgaError*         err) {

  if (flags & TgaFlags_Rle) {
    return tga_pixels_read_rle(pixels, channels, flags, width, height, input, outAlpha, err);
  }
  return tga_pixels_read_uncompressed(pixels, channels, flags, width, height, input, outAlpha, err);
}

static void
tga_load_fail(EcsWorld* world, const EcsEntityId entity, const String id, const TgaError err) {
  log_e(
      "Failed to parse Tga texture",
      log_param("id", fmt_text(id)),
      log_param("error", fmt_text(tga_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

void asset_load_tga(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  const bool isNormalmap = asset_texture_is_normalmap(id);

  Mem      data  = src->data;
  TgaError res   = TgaError_None;
  TgaFlags flags = 0;

  TgaHeader header;
  data = tga_read_header(data, &header, &res);
  if (res) {
    tga_load_fail(world, entity, id, res);
    goto Error;
  }
  if (header.colorMapType == TgaColorMapType_Present) {
    tga_load_fail(world, entity, id, TgaError_UnsupportedColorMap);
    goto Error;
  }
  const TgaChannels channels = tga_channels_from_bit_depth(header.imageSpec.bitsPerPixel);
  if (channels == TgaChannels_Invalid) {
    tga_load_fail(world, entity, id, TgaError_UnsupportedBitDepth);
    goto Error;
  }
  if (channels == TgaChannels_RGBA && header.imageSpec.descriptor.attributeDepth != 8) {
    tga_load_fail(world, entity, id, TgaError_UnsupportedAlphaChannelDepth);
    goto Error;
  }
  if (header.imageSpec.descriptor.interleave != TgaInterleave_None) {
    tga_load_fail(world, entity, id, TgaError_UnsupportedInterleaved);
    goto Error;
  }
  if (!tga_type_supported(header.imageType)) {
    tga_load_fail(world, entity, id, TgaError_UnsupportedNonTrueColor);
    goto Error;
  }
  if (!header.imageSpec.width || !header.imageSpec.height) {
    tga_load_fail(world, entity, id, TgaError_UnsupportedSize);
    goto Error;
  }
  if (header.imageSpec.width > tga_max_width || header.imageSpec.height > tga_max_height) {
    tga_load_fail(world, entity, id, TgaError_UnsupportedSize);
    goto Error;
  }
  if (header.imageType == TgaImageType_RleGrayscale ||
      header.imageType == TgaImageType_RleTrueColor) {
    flags |= TgaFlags_Rle;
  }
  if (header.imageSpec.descriptor.origin == TgaOrigin_UpperLeft ||
      header.imageSpec.descriptor.origin == TgaOrigin_UpperRight) {
    flags |= TgaFlags_YFlip;
  }

  if (data.size <= header.idLength) {
    tga_load_fail(world, entity, id, TgaError_Malformed);
    goto Error;
  }
  data = mem_consume(data, header.idLength); // Skip over the id field.

  const u32 width  = header.imageSpec.width;
  const u32 height = header.imageSpec.height;
  const Mem pixels = tga_pixels_alloc(g_allocHeap, channels, width, height);

  bool hasAlpha = false;
  data          = tga_pixels_read(pixels, channels, flags, width, height, data, &hasAlpha, &res);

  if (res) {
    tga_load_fail(world, entity, id, res);
    alloc_free(g_allocHeap, pixels);
    goto Error;
  }

  asset_repo_source_close(src);
  ecs_world_add_t(
      world,
      entity,
      AssetTextureComp,
      .format       = tga_texture_format(channels),
      .flags        = tga_texture_flags(channels, isNormalmap, hasAlpha),
      .width        = width,
      .height       = height,
      .pixelData    = pixels.ptr,
      .layers       = 1,
      .srcMipLevels = 1);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  return;

Error:
  asset_repo_source_close(src);
}
