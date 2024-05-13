#include "app_cli.h"
#include "core_alloc.h"
#include "core_bc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_math.h"
#include "core_path.h"
#include "core_time.h"
#include "log.h"

/**
 * BlockCompressionUtility - Utility to test texture block compression.
 *
 * NOTE: Contains an extremely simplistic tga parser that only supports uncompressed RGBA data which
 * uses lower-left as the image origin.
 */

typedef enum {
  BcuMode_QuantizeBc1,
  BcuMode_QuantizeBc3,
  BcuMode_QuantizeBc4,

  BcuMode_Count,
  BcuMode_Default = BcuMode_QuantizeBc1
} BcuMode;

static const String g_modeStrs[] = {
    string_static("quantize-bc1"),
    string_static("quantize-bc3"),
    string_static("quantize-bc4"),
};
ASSERT(array_elems(g_modeStrs) == BcuMode_Count, "Incorrect number of mode strings");

static bool bcu_validate_mode(const String input) {
  array_for_t(g_modeStrs, String, mode) {
    if (string_eq(*mode, input)) {
      return true;
    }
  }
  return false;
}

typedef enum {
  BcuResult_Success = 0,
  BcuResult_FileOpenFailed,
  BcuResult_FileMapFailed,
  BcuResult_FileWriteFailed,
  BcuResult_MemoryAllocationFailed,
  BcuResult_TgaFileTruncated,
  BcuResult_TgaUnsupportedColorMap,
  BcuResult_TgaUnsupportedImageType,
  BcuResult_TgaUnsupportedBitsPerPixel,
  BcuResult_TgaUnsupportedAttributeDepth,
  BcuResult_TgaUnsupportedImageOrigin,
  BcuResult_TgaUnsupportedInterleavedImage,
  BcuResult_ImageSizeNotAligned,

  BcuResult_Count,
} BcuResult;

static const String g_resultStrs[] = {
    string_static("Success"),
    string_static("Failed to open file"),
    string_static("Failed to map file"),
    string_static("Failed to write file"),
    string_static("Memory allocation failed"),
    string_static("Truncated tga file"),
    string_static("Color-mapped Tga images are not supported"),
    string_static("Unsupported Tga image type, only TrueColor and Grayscale supported (no rle)"),
    string_static("Unsupported Tga bits-per-pixel, only 32 (RGBA), 24 (RGB), 8 (R) are supported"),
    string_static("Unsupported Tga attribute depth, only 8 bit Tga alpha is supported"),
    string_static("Unsupported Tga image origin, only 'BottomLeft' is supported"),
    string_static("Interleaved Tga images are not supported"),
    string_static("Image dimensions need to be 4 pixel aligned"),
};
ASSERT(array_elems(g_resultStrs) == BcuResult_Count, "Incorrect number of result strings");

typedef struct {
  u16 width, height;
} BcuSize;

typedef struct {
  BcuSize            size;
  const BcColor8888* pixels;
} BcuImage;

static BcuResult bcu_image_load(const String path, BcuImage* out) {
  BcuResult  result;
  File*      fileHandle = null;
  FileResult fileRes;
  if ((fileRes = file_create(g_allocHeap, path, FileMode_Open, FileAccess_Read, &fileHandle))) {
    result = BcuResult_FileOpenFailed;
    goto End;
  }
  Mem data;
  if ((fileRes = file_map(fileHandle, &data))) {
    result = BcuResult_FileOpenFailed;
    goto End;
  }
  if (data.size < 18) {
    result = BcuResult_TgaFileTruncated;
    goto End;
  }
  u8      colorMapType, imageType, bitsPerPixel, imageSpecDescriptorRaw;
  BcuSize size;

  data = mem_consume(data, 1); // Skip over 'idLength'.
  data = mem_consume_u8(data, &colorMapType);
  data = mem_consume_u8(data, &imageType);
  data = mem_consume(data, 5); // Skip over 'ColorMapSpec'.
  data = mem_consume(data, 4); // Skip over 'origin'.
  data = mem_consume_le_u16(data, &size.width);
  data = mem_consume_le_u16(data, &size.height);
  data = mem_consume_u8(data, &bitsPerPixel);
  data = mem_consume_u8(data, &imageSpecDescriptorRaw);

  const u8 imageAttributeDepth = imageSpecDescriptorRaw & u8_lit(0b1111);
  const u8 imageOrigin         = imageSpecDescriptorRaw & u8_lit(0b110000);
  const u8 imageInterleave     = imageSpecDescriptorRaw & u8_lit(0b11000000);

  if (colorMapType != 0 /* Absent*/) {
    result = BcuResult_TgaUnsupportedColorMap;
    goto End;
  }
  if (imageType != 2 /* TrueColor */ && imageType != 3 /* Grayscale */) {
    result = BcuResult_TgaUnsupportedImageType;
    goto End;
  }
  if (bitsPerPixel != 32 && bitsPerPixel != 24 && bitsPerPixel != 8) {
    result = BcuResult_TgaUnsupportedBitsPerPixel;
    goto End;
  }
  if (bitsPerPixel == 32 && imageAttributeDepth != 8) {
    result = BcuResult_TgaUnsupportedAttributeDepth;
    goto End;
  }
  if (imageOrigin != 0 /* LowerLeft */) {
    result = BcuResult_TgaUnsupportedImageOrigin;
    goto End;
  }
  if (imageInterleave != 0 /* None */) {
    result = BcuResult_TgaUnsupportedInterleavedImage;
    goto End;
  }
  if (!bits_aligned(size.width, 4) || !bits_aligned(size.height, 4)) {
    result = BcuResult_ImageSizeNotAligned;
    goto End;
  }
  if (data.size < (size.width * size.height * bits_to_bytes(bitsPerPixel))) {
    result = BcuResult_TgaFileTruncated;
    goto End;
  }
  const usize  pixelCount = size.width * size.height;
  BcColor8888* pixels     = alloc_array_t(g_allocHeap, BcColor8888, pixelCount);
  if (!pixels) {
    result = BcuResult_MemoryAllocationFailed;
    goto End;
  }
  u8* pixelData = data.ptr;
  for (usize i = 0; i != pixelCount; ++i, pixelData += bits_to_bytes(bitsPerPixel)) {
    switch (bitsPerPixel) {
    case 32:
      pixels[i].b = pixelData[0];
      pixels[i].g = pixelData[1];
      pixels[i].r = pixelData[2];
      pixels[i].a = pixelData[3];
      break;
    case 24:
      pixels[i].b = pixelData[0];
      pixels[i].g = pixelData[1];
      pixels[i].r = pixelData[2];
      pixels[i].a = 255;
      break;
    case 8:
      pixels[i].r = pixelData[0];
      pixels[i].g = 0;
      pixels[i].b = 0;
      pixels[i].a = 255;
      break;
    default:
      UNREACHABLE
    }
  }
  result = BcuResult_Success;
  *out   = (BcuImage){.size = size, .pixels = pixels};

End:
  if (fileHandle) {
    file_destroy(fileHandle);
  }
  return result;
}

static void bcu_image_destroy(BcuImage* image) {
  if (image->pixels) {
    alloc_free_array_t(g_allocHeap, image->pixels, image->size.width * image->size.height);
  }
}

static BcuResult bcu_image_write(const BcuSize size, const BcColor8888* pixels, const String path) {
  const usize headerSize    = 18;
  const usize pixelCount    = size.width * size.height;
  const usize pixelDataSize = pixelCount * sizeof(BcColor8888);
  const Mem   data          = alloc_alloc(g_allocHeap, headerSize + pixelDataSize, 1);
  if (!mem_valid(data)) {
    return BcuResult_MemoryAllocationFailed;
  }
  const u8 attributeDepth      = 8;
  const u8 imageSpecDescriptor = attributeDepth; // imageOrigin and imageInterleave both zero.

  Mem buffer = data;
  buffer     = mem_write_u8_zero(buffer, 2);              // idLength and colorMapType.
  buffer     = mem_write_u8(buffer, 2 /* TrueColor */);   // imageType.
  buffer     = mem_write_u8_zero(buffer, 9);              // colorMapSpec and origin.
  buffer     = mem_write_le_u16(buffer, size.width);      // image width.
  buffer     = mem_write_le_u16(buffer, size.height);     // image height.
  buffer     = mem_write_u8(buffer, 32);                  // bitsPerPixel.
  buffer     = mem_write_u8(buffer, imageSpecDescriptor); // imageSpecDescriptor.

  // pixel data.
  u8* outPixelData = buffer.ptr;
  for (usize i = 0; i != pixelCount; ++i, outPixelData += 4) {
    outPixelData[0] = pixels[i].b;
    outPixelData[1] = pixels[i].g;
    outPixelData[2] = pixels[i].r;
    outPixelData[3] = pixels[i].a;
  }

  String pathWithExt;
  if (string_eq(path_extension(path), string_lit("tga"))) {
    pathWithExt = path;
  } else {
    pathWithExt = fmt_write_scratch("{}.tga", fmt_path(path));
  }

  const FileResult writeRes = file_write_to_path_sync(pathWithExt, data);
  alloc_free(g_allocHeap, data);
  return writeRes ? BcuResult_FileWriteFailed : BcuResult_Success;
}

INLINE_HINT static f64 bcu_sqr(const f64 val) { return val * val; }

typedef enum {
  BcuChannelMask_R   = 1 << 0,
  BcuChannelMask_G   = 1 << 1,
  BcuChannelMask_B   = 1 << 2,
  BcuChannelMask_A   = 1 << 3,
  BcuChannelMask_RGB = BcuChannelMask_R | BcuChannelMask_G | BcuChannelMask_B,
} BcuChannelMask;

/**
 * Compute the root mean square error between the sets of pixels.
 */
static f64 bcu_image_diff(
    const BcuSize size, const BcuChannelMask mask, const BcColor8888* pA, const BcColor8888* pB) {
  const usize pixelCount = size.width * size.height;
  f64         sum        = 0;
  for (usize i = 0; i != pixelCount; ++i) {
    if (mask & BcuChannelMask_R) {
      sum += bcu_sqr((f64)pB[i].r - (f64)pA[i].r);
    }
    if (mask & BcuChannelMask_G) {
      sum += bcu_sqr((f64)pB[i].g - (f64)pA[i].g);
    }
    if (mask & BcuChannelMask_B) {
      sum += bcu_sqr((f64)pB[i].b - (f64)pA[i].b);
    }
    if (mask & BcuChannelMask_A) {
      sum += bcu_sqr((f64)pB[i].a - (f64)pA[i].a);
    }
  }
  return math_sqrt_f64(sum / pixelCount);
}

static u32 bcu_block_count(const BcuSize size) {
  return math_max(size.width / 4, 1) * math_max(size.height / 4, 1);
}

static void bcu_blocks_extract(const BcuSize size, const BcColor8888* inPtr, Bc0Block* outPtr) {
  const TimeSteady startTime = time_steady_clock();

  for (u32 y = 0; y < size.height; y += 4, inPtr += size.width * 4) {
    for (u32 x = 0; x < size.width; x += 4, ++outPtr) {
      bc0_extract4(inPtr + x, size.width, outPtr);
    }
  }

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  log_i(
      "Extracted {} blocks",
      log_param("blocks", fmt_int(bcu_block_count(size))),
      log_param("duration", fmt_duration(dur)));
}

static void bcu_blocks_scanout(const BcuSize size, const Bc0Block* inPtr, BcColor8888* outPtr) {
  const TimeSteady startTime = time_steady_clock();

  for (u32 y = 0; y < size.height; y += 4, outPtr += size.width * 4) {
    for (u32 x = 0; x < size.width; x += 4, ++inPtr) {
      bc0_scanout4(inPtr, size.width, outPtr + x);
    }
  }

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  log_i(
      "Scanned out to {} pixels",
      log_param("pixels", fmt_int(size.width * size.height)),
      log_param("duration", fmt_duration(dur)));
}

static void bcu_blocks_quantize_bc1(Bc0Block* blocks, const u32 blockCount) {
  const TimeSteady startTime = time_steady_clock();

  Bc1Block encodedBlock;
  for (u32 i = 0; i != blockCount; ++i) {
    bc1_encode(blocks + i, &encodedBlock);
    bc1_decode(&encodedBlock, blocks + i);
  }

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  log_i(
      "Quantized to bc1",
      log_param("bc1-size", fmt_size(blockCount * sizeof(Bc1Block))),
      log_param("duration", fmt_duration(dur)));
}

static void bcu_blocks_quantize_bc3(Bc0Block* blocks, const u32 blockCount) {
  const TimeSteady startTime = time_steady_clock();

  Bc3Block encodedBlock;
  for (u32 i = 0; i != blockCount; ++i) {
    bc3_encode(blocks + i, &encodedBlock);
    bc3_decode(&encodedBlock, blocks + i);
  }

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  log_i(
      "Quantized to bc3",
      log_param("bc3-size", fmt_size(blockCount * sizeof(Bc3Block))),
      log_param("duration", fmt_duration(dur)));
}

static void bcu_blocks_quantize_bc4(Bc0Block* blocks, const u32 blockCount) {
  const TimeSteady startTime = time_steady_clock();

  Bc4Block encodedBlock;
  for (u32 i = 0; i != blockCount; ++i) {
    bc4_encode(blocks + i, &encodedBlock);
    bc4_decode(&encodedBlock, blocks + i);
  }

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  log_i(
      "Quantized to bc4",
      log_param("bc4-size", fmt_size(blockCount * sizeof(Bc3Block))),
      log_param("duration", fmt_duration(dur)));
}

static BcuResult bcu_run(const BcuMode mode, const BcuImage* input, const String outputPath) {
  const u32 blockCount = bcu_block_count(input->size);
  Bc0Block* blocks     = alloc_array_t(g_allocHeap, Bc0Block, blockCount);

  bcu_blocks_extract(input->size, input->pixels, blocks);

  const usize  encodedPixelCount = blockCount * 16;
  BcColor8888* encodedPixels     = alloc_array_t(g_allocHeap, BcColor8888, encodedPixelCount);

  switch (mode) {
  case BcuMode_QuantizeBc1:
    bcu_blocks_quantize_bc1(blocks, blockCount);
    break;
  case BcuMode_QuantizeBc3:
    bcu_blocks_quantize_bc3(blocks, blockCount);
    break;
  case BcuMode_QuantizeBc4:
    bcu_blocks_quantize_bc4(blocks, blockCount);
    break;
  default:
    diag_crash_msg("Unsupported mode");
    break;
  }

  bcu_blocks_scanout(input->size, blocks, encodedPixels);

  const f64 diffRgb = bcu_image_diff(input->size, BcuChannelMask_RGB, input->pixels, encodedPixels);
  const f64 diffR   = bcu_image_diff(input->size, BcuChannelMask_R, input->pixels, encodedPixels);
  const f64 diffA   = bcu_image_diff(input->size, BcuChannelMask_A, input->pixels, encodedPixels);
  const BcuResult result = bcu_image_write(input->size, encodedPixels, outputPath);
  log_i(
      "Wrote output image",
      log_param("path", fmt_path(outputPath)),
      log_param("diff-rgb", fmt_float(diffRgb)),
      log_param("diff-r", fmt_float(diffR)),
      log_param("diff-a", fmt_float(diffA)));

  alloc_free_array_t(g_allocHeap, encodedPixels, encodedPixelCount);
  alloc_free_array_t(g_allocHeap, blocks, blockCount);
  return result;
}

static CliId g_optMode, g_optInput, g_optOutput, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Texture block compression utility."));

  g_optMode = cli_register_arg(app, string_lit("mode"), CliOptionFlags_None);
  cli_register_desc_choice_array(app, g_optMode, string_empty, g_modeStrs, BcuMode_Default);
  cli_register_validator(app, g_optMode, bcu_validate_mode);

  g_optInput = cli_register_flag(app, 'i', string_lit("input"), CliOptionFlags_Required);
  cli_register_desc(app, g_optInput, string_lit("Input image path."));
  cli_register_validator(app, g_optInput, cli_validate_file_regular);

  g_optOutput = cli_register_flag(app, 'o', string_lit("output"), CliOptionFlags_Required);
  cli_register_desc(app, g_optOutput, string_lit("Output image path."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optMode, g_optInput, g_optOutput);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdout);
    return 0;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, ~LogMask_Debug));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  const usize   modeRaw    = cli_read_choice_array(invoc, g_optMode, g_modeStrs, BcuMode_Default);
  const BcuMode mode       = (BcuMode)modeRaw;
  const String  inputPath  = cli_read_string(invoc, g_optInput, string_empty);
  const String  outputPath = cli_read_string(invoc, g_optOutput, string_empty);

  BcuImage  input = {0};
  BcuResult result;

  if ((result = bcu_image_load(inputPath, &input))) {
    log_e("Input image unsupported", log_param("error", fmt_text(g_resultStrs[result])));
    goto End;
  }

  const usize pixelCount    = input.size.width * input.size.height;
  const usize pixelDataSize = pixelCount * sizeof(BcColor8888);
  log_i(
      "Opened input image",
      log_param("path", fmt_path(inputPath)),
      log_param("width", fmt_int(input.size.width)),
      log_param("height", fmt_int(input.size.height)),
      log_param("pixels", fmt_int(pixelCount)),
      log_param("data", fmt_size(pixelDataSize)));

  result = bcu_run(mode, &input, outputPath);

End:
  bcu_image_destroy(&input);
  return result == BcuResult_Success ? 0 : 1;
}
