#include "app_cli.h"
#include "core_alloc.h"
#include "core_bc.h"
#include "core_bits.h"
#include "core_file.h"
#include "core_path.h"
#include "log.h"

/**
 * BlockCompressionUtility - Utility to test texture block compression.
 *
 * NOTE: Contains an extremely simplistic tga parser that only supports uncompressed RGBA data which
 * uses lower-left as the image origin.
 */

typedef enum {
  BcuResult_Success = 0,
  BcuResult_MemoryAllocationFailed,
  BcuResult_TgaFileTruncated,
  BcuResult_TgaUnsupportedColorMap,
  BcuResult_TgaUnsupportedImageType,
  BcuResult_TgaUnsupportedBitsPerPixel,
  BcuResult_TgaUnsupportedAttributeDepth,
  BcuResult_TgaUnsupportedImageOrigin,
  BcuResult_TgaUnsupportedInterleavedImage,

  BcuResult_Count,
} BcuResult;

static String bcu_result_str(const BcuResult res) {
  static const String g_msgs[] = {
      string_static("Success"),
      string_static("Memory allocation failed"),
      string_static("Truncated tga file"),
      string_static("Color-mapped Tga images are not supported"),
      string_static("Unsupported Tga image type, only 'TrueColor' is supported (no rle)"),
      string_static("Unsupported Tga bits-per-pixel, only 32 bits (RGBA is supported)"),
      string_static("Unsupported Tga attribute depth, only 8 bit Tga alpha is supported"),
      string_static("Unsupported Tga image origin, only 'BottomLeft' is supported"),
      string_static("Interleaved Tga images are not supported"),
  };
  ASSERT(array_elems(g_msgs) == BcuResult_Count, "Incorrect number of result messages");
  return g_msgs[res];
}

typedef struct {
  u16 width, height;
} BcuSize;

typedef struct {
  BcuSize            size;
  const BcColor8888* pixels;
} BcuImage;

static BcuResult bcu_image_read(Mem input, BcuImage* out) {
  if (input.size < 18) {
    return BcuResult_TgaFileTruncated;
  }
  u8      colorMapType, imageType, bitsPerPixel, imageSpecDescriptorRaw;
  BcuSize size;

  input = mem_consume(input, 1); // Skip over 'idLength'.
  input = mem_consume_u8(input, &colorMapType);
  input = mem_consume_u8(input, &imageType);
  input = mem_consume(input, 5); // Skip over 'ColorMapSpec'.
  input = mem_consume(input, 4); // Skip over 'origin'.
  input = mem_consume_le_u16(input, &size.width);
  input = mem_consume_le_u16(input, &size.height);
  input = mem_consume_u8(input, &bitsPerPixel);
  input = mem_consume_u8(input, &imageSpecDescriptorRaw);

  const u8 imageAttributeDepth = imageSpecDescriptorRaw & u8_lit(0b1111);
  const u8 imageOrigin         = imageSpecDescriptorRaw & u8_lit(0b110000);
  const u8 imageInterleave     = imageSpecDescriptorRaw & u8_lit(0b11000000);

  if (colorMapType != 0 /* Absent*/) {
    return BcuResult_TgaUnsupportedColorMap;
  }
  if (imageType != 2 /* TrueColor */) {
    return BcuResult_TgaUnsupportedImageType;
  }
  if (bitsPerPixel != 32) {
    return BcuResult_TgaUnsupportedBitsPerPixel;
  }
  if (imageAttributeDepth != 8) {
    return BcuResult_TgaUnsupportedAttributeDepth;
  }
  if (imageOrigin != 0 /* LowerLeft */) {
    return BcuResult_TgaUnsupportedImageOrigin;
  }
  if (imageInterleave != 0 /* None */) {
    return BcuResult_TgaUnsupportedInterleavedImage;
  }
  if (input.size < (size.width * size.height * sizeof(BcColor8888))) {
    return BcuResult_TgaUnsupportedColorMap;
  }
  *out = (BcuImage){.size = size, .pixels = input.ptr};
  return BcuResult_Success;
}

static BcuResult bcu_image_write(const BcuSize size, const BcColor8888* pixels, const String path) {
  const usize headerSize    = 18;
  const usize pixelDataSize = size.width * size.height * sizeof(BcColor8888);
  const Mem   data          = alloc_alloc(g_alloc_heap, headerSize + pixelDataSize, 8);
  if (!mem_valid(data)) {
    return BcuResult_MemoryAllocationFailed;
  }

  Mem buffer = data;
  buffer     = mem_write_u8_zero(buffer, 2);            // idLength and colorMapType.
  buffer     = mem_write_u8(buffer, 2 /* TrueColor */); // imageType.
  buffer     = mem_write_u8_zero(buffer, 9);            // colorMapSpec and origin.
  buffer     = mem_write_le_u16(buffer, size.width);    // image width.
  buffer     = mem_write_le_u16(buffer, size.height);   // image height.
  buffer     = mem_write_u8(buffer, 32);                // bitsPerPixel.
  buffer     = mem_write_u8(buffer, 0b100000);          // imageSpecDescriptor.
  mem_cpy(buffer, mem_create(pixels, pixelDataSize));   // pixel data.

  String pathWithExt;
  if (string_eq(path_extension(path), string_lit("tga"))) {
    pathWithExt = path;
  } else {
    pathWithExt = fmt_write_scratch("{}.tga", fmt_path(path));
  }

  file_write_to_path_sync(pathWithExt, data);
  return BcuResult_Success;
}

static bool bcu_run(const String inputPath, const String outputPath) {
  bool success = false;

  File*      inFile = null;
  FileResult inRes;
  if ((inRes = file_create(g_alloc_heap, inputPath, FileMode_Open, FileAccess_Read, &inFile))) {
    log_e("Failed to open input file", log_param("path", fmt_path(inputPath)));
    goto End;
  }
  Mem inData;
  if ((inRes = file_map(inFile, &inData))) {
    log_e("Failed to map input file", log_param("path", fmt_path(inputPath)));
    goto End;
  }
  BcuImage  inImage;
  BcuResult inResult;
  if ((inResult = bcu_image_read(inData, &inImage))) {
    log_e("Input image unsupported", log_param("error", fmt_text(bcu_result_str(inResult))));
    goto End;
  }
  if (!bits_ispow2(inImage.size.width) || !bits_ispow2(inImage.size.height)) {
    log_e("Input image dimensions needs to be a power of two");
    goto End;
  }
  if (inImage.size.width < 4 || inImage.size.height < 4) {
    log_e("Input image dimensions too small (needs to be at least 4 pixels)");
    goto End;
  }

  // const u32          pixelCount = header.width * header.height;
  // const BcColor8888* pixels     = inData.ptr;
  (void)bcu_image_write;
  (void)outputPath;

  success = true;

End:
  if (inFile) {
    file_destroy(inFile);
  }
  return success;
}

static CliId g_inputFlag, g_outputFlag, g_helpFlag;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Texture block compression utility."));

  g_inputFlag = cli_register_flag(app, 'i', string_lit("input"), CliOptionFlags_Required);
  cli_register_desc(app, g_inputFlag, string_lit("Input image path."));
  cli_register_validator(app, g_inputFlag, cli_validate_file_regular);

  g_outputFlag = cli_register_flag(app, 'o', string_lit("output"), CliOptionFlags_Required);
  cli_register_desc(app, g_outputFlag, string_lit("Output image path."));

  g_helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_helpFlag, string_lit("Display this help page."));
  cli_register_exclusions(app, g_helpFlag, g_inputFlag, g_outputFlag);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_helpFlag)) {
    cli_help_write_file(app, g_file_stdout);
    return 0;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_alloc_heap, ~LogMask_Debug));
  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  const String inputPath  = cli_read_string(invoc, g_inputFlag, string_empty);
  const String outputPath = cli_read_string(invoc, g_outputFlag, string_empty);

  return bcu_run(inputPath, outputPath) ? 0 : 1;
}
