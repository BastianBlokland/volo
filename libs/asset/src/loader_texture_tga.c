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
 * Supports 24 bit (rgb) and 32 bit (rgba) and optionally rle compressed.
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
  TgaFlags_Rle   = 1 << 0,
  TgaFlags_YFlip = 1 << 1,
  TgaFlags_Alpha = 1 << 2,
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
      string_static("Unsupported bit depth, only 24 bit (RGB) and 32 bit (RGBA) are supported"),
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

static u32 tga_index(const u32 x, const u32 y, const u32 width, const u32 height, TgaFlags flags) {
  // Either fill pixels from bottom to top - left to right, or top to bottom - left to right.
  return ((flags & TgaFlags_YFlip) ? (height - 1 - y) * width : y * width) + x;
}

static void tga_read_pixel_unchecked(u8* data, const TgaFlags flags, AssetTexturePixelB4* out) {
  out->b = data[0];
  out->g = data[1];
  out->r = data[2];
  if (flags & TgaFlags_Alpha) {
    out->a = data[3];
  } else {
    out->a = 255; // Treat images without alpha as fully opaque.
  }
}

INLINE_HINT static void tga_mem_consume(Mem* mem, const usize amount) {
  mem->ptr = bits_ptr_offset(mem->ptr, amount);
  mem->size -= amount;
}

static Mem tga_read_pixels_uncompressed(
    Mem                  input,
    const u32            width,
    const u32            height,
    const TgaFlags       flags,
    AssetTexturePixelB4* out,
    TgaError*            err) {

  const u32 pixelCount = width * height;
  const u32 pixelSize  = flags & TgaFlags_Alpha ? 4 : 3;

  if (input.size < pixelCount * pixelSize) {
    *err = TgaError_MalformedPixels;
    return input;
  }
  u8* src = mem_begin(input);
  for (u32 y = 0; y != height; ++y) {
    for (u32 x = 0; x != width; ++x) {
      tga_read_pixel_unchecked(src, flags, &out[tga_index(x, y, width, height, flags)]);
      src += pixelSize;
    }
  }
  *err = TgaError_None;
  return mem_consume(input, pixelCount * pixelSize);
}

static Mem tga_read_pixels_rle(
    Mem                  input,
    const u32            width,
    const u32            height,
    const TgaFlags       flags,
    AssetTexturePixelB4* out,
    TgaError*            err) {

  const u32 pixelSize      = flags & TgaFlags_Alpha ? 4 : 3;
  u32       packetRem      = 0; // How many pixels are left in the current rle packet.
  u32       packetRefPixel = u32_max;
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
        if (UNLIKELY(input.size <= pixelSize)) {
          *err = TgaError_MalformedRlePixels;
          return input;
        }
        u8 packetHeader = *mem_begin(input);
        tga_mem_consume(&input, 1);
        const bool isRlePacket = (packetHeader & 0b10000000) != 0; // Msb indicates packet type.
        packetRefPixel         = isRlePacket ? i : u32_max;
        packetRem              = packetHeader & 0b01111111; // Remaining 7 bits are the rep count.

        if (UNLIKELY(!isRlePacket && input.size < (packetRem + 1) * pixelSize)) {
          *err = TgaError_MalformedRlePixels;
          return input;
        }
      } else {
        // This pixel is still part of the same packet.
        --packetRem;
      }

      if (packetRefPixel < i) {
        // There is a reference pixel; Use that instead of reading a new one.
        out[i] = out[packetRefPixel];
      } else {
        // No reference pixel; Read a new pixel value.
        tga_read_pixel_unchecked(mem_begin(input), flags, &out[i]);
        tga_mem_consume(&input, pixelSize);
      }
    }
  }
  *err = TgaError_None;
  return input;
}

static Mem tga_read_pixels(
    Mem                  input,
    const u32            width,
    const u32            height,
    const TgaFlags       flags,
    AssetTexturePixelB4* out,
    TgaError*            err) {

  if (flags & TgaFlags_Rle) {
    return tga_read_pixels_rle(input, width, height, flags, out, err);
  }
  return tga_read_pixels_uncompressed(input, width, height, flags, out, err);
}

static void tga_load_fail(EcsWorld* world, const EcsEntityId entity, const TgaError err) {
  log_e("Failed to parse Tga texture", log_param("error", fmt_text(tga_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

static AssetTextureFlags tga_texture_flags(const bool isNormalmap) {
  AssetTextureFlags flags = AssetTextureFlags_MipMaps;
  if (isNormalmap) {
    flags |= AssetTextureFlags_NormalMap;
  } else {
    flags |= AssetTextureFlags_Srgb;
  }
  return flags;
}

void asset_load_tga(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  const bool isNormalmap = asset_texture_is_normalmap(id);

  Mem      data  = src->data;
  TgaError res   = TgaError_None;
  TgaFlags flags = 0;

  TgaHeader header;
  data = tga_read_header(data, &header, &res);
  if (res) {
    tga_load_fail(world, entity, res);
    goto Error;
  }
  if (header.colorMapType == TgaColorMapType_Present) {
    tga_load_fail(world, entity, TgaError_UnsupportedColorMap);
    goto Error;
  }
  if (header.imageSpec.bitsPerPixel != 24 && header.imageSpec.bitsPerPixel != 32) {
    tga_load_fail(world, entity, TgaError_UnsupportedBitDepth);
    goto Error;
  }
  if (header.imageSpec.bitsPerPixel == 32) {
    flags |= TgaFlags_Alpha;
  }
  if (flags & TgaFlags_Alpha && header.imageSpec.descriptor.attributeDepth != 8) {
    tga_load_fail(world, entity, TgaError_UnsupportedAlphaChannelDepth);
    goto Error;
  }
  if (header.imageSpec.descriptor.interleave != TgaInterleave_None) {
    tga_load_fail(world, entity, TgaError_UnsupportedInterleaved);
    goto Error;
  }
  if (header.imageType != TgaImageType_TrueColor && header.imageType != TgaImageType_RleTrueColor) {
    tga_load_fail(world, entity, TgaError_UnsupportedNonTrueColor);
    goto Error;
  }
  if (!header.imageSpec.width || !header.imageSpec.height) {
    tga_load_fail(world, entity, TgaError_UnsupportedSize);
    goto Error;
  }
  if (header.imageSpec.width > tga_max_width || header.imageSpec.height > tga_max_height) {
    tga_load_fail(world, entity, TgaError_UnsupportedSize);
    goto Error;
  }
  if (header.imageType == TgaImageType_RleTrueColor) {
    flags |= TgaFlags_Rle;
  }
  if (header.imageSpec.descriptor.origin == TgaOrigin_UpperLeft ||
      header.imageSpec.descriptor.origin == TgaOrigin_UpperRight) {
    flags |= TgaFlags_YFlip;
  }

  if (data.size <= header.idLength) {
    tga_load_fail(world, entity, TgaError_Malformed);
    goto Error;
  }
  data = mem_consume(data, header.idLength); // Skip over the id field.

  const u32            width  = header.imageSpec.width;
  const u32            height = header.imageSpec.height;
  AssetTexturePixelB4* pixels = alloc_array_t(g_alloc_heap, AssetTexturePixelB4, width * height);
  data                        = tga_read_pixels(data, width, height, flags, pixels, &res);
  if (res) {
    tga_load_fail(world, entity, res);
    alloc_free_array_t(g_alloc_heap, pixels, width * height);
    goto Error;
  }

  asset_repo_source_close(src);
  ecs_world_add_t(
      world,
      entity,
      AssetTextureComp,
      .type     = AssetTextureType_Byte,
      .channels = AssetTextureChannels_Four,
      .flags    = tga_texture_flags(isNormalmap),
      .width    = width,
      .height   = height,
      .pixelsB4 = pixels);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  return;

Error:
  asset_repo_source_close(src);
}
