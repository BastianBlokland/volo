#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "loader_texture_internal.h"
#include "repo_internal.h"

/**
 * Portable Pixmap Format.
 * Ascii format P3 and binary format P6 are both suported.
 * Format specification: https://en.wikipedia.org/wiki/Netpbm
 */

#define ppm_max_width (1024 * 16)
#define ppm_max_height (1024 * 16)

typedef enum {
  PixmapType_Unknown,
  PixmapType_Ascii,
  PixmapType_Binary,
} PixmapType;

typedef struct {
  PixmapType type;
  u64        width, height;
  u64        maxValue;
} PixmapHeader;

typedef enum {
  PixmapError_None = 0,
  PixmapError_MalformedPixels,
  PixmapError_MalformedType,
  PixmapError_UnsupportedBitDepth,
  PixmapError_UnsupportedSize,

  PixmapError_Count,
} PixmapError;

static String pixmap_error_str(PixmapError res) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Malformed pixmap pixel data"),
      string_static("Malformed pixmap type, expected 'P3' or 'P6'"),
      string_static("Unsupported bit depth, only 24 bit (RGB) is supported"),
      string_static("Unsupported image size"),
  };
  ASSERT(array_elems(g_msgs) == PixmapError_Count, "Incorrect number of pixmap-error messages");
  return g_msgs[res];
}

static String ppm_consume_whitespace_or_comment(String input) {
  while (!string_is_empty(input)) {
    input = format_read_whitespace(input, null);
    if (string_is_empty(input) || *string_begin(input) != '#') {
      // Not whitespace or the start of a comment; stop consuming.
      return input;
    }
    // Consume the rest of the line as its part of the comment.
    input = format_read_line(input, null);
  }
  return input;
}

static String ppm_read_type(const String input, PixmapType* out) {
  u8     ch;
  String inputRem = format_read_char(input, &ch);
  *out            = PixmapType_Unknown;
  if (ch != 'P') {
    return input;
  }
  inputRem = format_read_char(inputRem, &ch);
  switch (ch) {
  case '3':
    *out = PixmapType_Ascii;
    break;
  case '6':
    *out = PixmapType_Binary;
    break;
  }
  return inputRem;
}

static String ppm_read_header(String input, PixmapHeader* out) {
  input = ppm_consume_whitespace_or_comment(input);
  input = ppm_read_type(input, &out->type);
  input = ppm_consume_whitespace_or_comment(input);
  input = format_read_u64(input, &out->width, 10);
  input = ppm_consume_whitespace_or_comment(input);
  input = format_read_u64(input, &out->height, 10);
  input = ppm_consume_whitespace_or_comment(input);
  input = format_read_u64(input, &out->maxValue, 10);
  return input;
}

static String ppm_read_pixels_ascii(
    String input, PixmapHeader* header, u8* out /* u8[width * height * 4] */, PixmapError* err) {
  u64 r, g, b;

  /**
   * NOTE: PPM images use the top-left as the origin, while the Volo project uses the bottom-left,
   * so we have to remap the y axis.
   */

  for (u32 y = (u32)header->height; y-- != 0;) {
    for (u32 x = 0; x != header->width; ++x) {

      input = ppm_consume_whitespace_or_comment(input);
      input = format_read_u64(input, &r, 10);
      input = ppm_consume_whitespace_or_comment(input);
      input = format_read_u64(input, &g, 10);
      input = ppm_consume_whitespace_or_comment(input);
      input = format_read_u64(input, &b, 10);

      const u32 index = y * (u32)header->height + x;

      out[index * 4 + 0] = (u8)r;
      out[index * 4 + 1] = (u8)g;
      out[index * 4 + 2] = (u8)b;
      out[index * 4 + 3] = 255;
    }
  }
  *err = PixmapError_None;
  return input;
}

static String ppm_read_pixels_binary(
    String input, PixmapHeader* header, u8* out /* u8[width * height * 4] */, PixmapError* err) {

  const u32 pixelCount = (u32)(header->width * header->height);
  if (input.size <= pixelCount * 3) {
    *err = PixmapError_MalformedPixels;
    return input;
  }

  /**
   * A single character should separate the header and the data.
   * NOTE: this means you cannot use a windows style line-ending between the header and data, but a
   * space would work fine.
   */
  input = string_consume(input, 1);

  /**
   * NOTE: PPM images use the top-left as the origin, while the Volo project uses the bottom-left,
   * so we have to remap the y axis.
   *
   * NOTE: Follows the same to RGBA conversion rules as the Vulkan spec:
   * https://registry.khronos.org/vulkan/specs/1.0/html/chap16.html#textures-conversion-to-rgba
   */

  u8* data = input.ptr;
  for (u32 y = (u32)header->height; y-- != 0;) {
    for (u32 x = 0; x != header->width; ++x) {
      const u32 index = y * (u32)header->height + x;

      out[index * 4 + 0] = data[0];
      out[index * 4 + 1] = data[1];
      out[index * 4 + 2] = data[2];
      out[index * 4 + 3] = 255;

      data += 3;
    }
  }

  *err = PixmapError_None;
  return string_consume(input, pixelCount * 3);
}

static String ppm_read_pixels(String input, PixmapHeader* header, u8* out, PixmapError* err) {
  if (header->type == PixmapType_Ascii) {
    return ppm_read_pixels_ascii(input, header, out, err);
  }
  return ppm_read_pixels_binary(input, header, out, err);
}

static void
ppm_load_fail(EcsWorld* world, const EcsEntityId entity, const String id, const PixmapError err) {
  log_e(
      "Failed to parse Pixmap texture",
      log_param("id", fmt_text(id)),
      log_param("error", fmt_text(pixmap_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

static AssetTextureFlags ppm_texture_flags(const bool isNormalmap) {
  AssetTextureFlags flags = AssetTextureFlags_GenerateMipMaps;
  if (isNormalmap) {
    flags |= AssetTextureFlags_NormalMap;
  } else {
    flags |= AssetTextureFlags_Srgb;
  }
  return flags;
}

void asset_load_ppm(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  const bool isNormalmap = asset_texture_is_normalmap(id);

  String      input = src->data;
  PixmapError res   = PixmapError_None;

  PixmapHeader header;
  input = ppm_read_header(input, &header);
  if (header.type == PixmapType_Unknown) {
    ppm_load_fail(world, entity, id, PixmapError_MalformedType);
    goto Error;
  }
  if (!header.width || !header.height) {
    ppm_load_fail(world, entity, id, PixmapError_UnsupportedSize);
    goto Error;
  }
  if (header.width > ppm_max_width || header.height > ppm_max_height) {
    ppm_load_fail(world, entity, id, PixmapError_UnsupportedSize);
    goto Error;
  }
  if (header.maxValue != 255) {
    ppm_load_fail(world, entity, id, PixmapError_UnsupportedBitDepth);
    goto Error;
  }

  const u32 width  = (u32)header.width;
  const u32 height = (u32)header.height;
  u8*       pixels = alloc_alloc(g_allocHeap, width * height * 4, 4).ptr;
  input            = ppm_read_pixels(input, &header, pixels, &res);
  if (res) {
    ppm_load_fail(world, entity, id, res);
    alloc_free_array_t(g_allocHeap, pixels, width * height);
    goto Error;
  }

  asset_repo_source_close(src);
  ecs_world_add_t(
      world,
      entity,
      AssetTextureComp,
      .format       = AssetTextureFormat_u8_rgba,
      .flags        = ppm_texture_flags(isNormalmap),
      .width        = width,
      .height       = height,
      .pixelData    = pixels,
      .layers       = 1,
      .srcMipLevels = 1);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  return;

Error:
  asset_repo_source_close(src);
}
