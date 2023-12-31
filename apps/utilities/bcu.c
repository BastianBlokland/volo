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

static String bcu_result_str(const BcuResult res) {
  static const String g_msgs[] = {
      string_static("Success"),
      string_static("Failed to open file"),
      string_static("Failed to map file"),
      string_static("Failed to write file"),
      string_static("Memory allocation failed"),
      string_static("Truncated tga file"),
      string_static("Color-mapped Tga images are not supported"),
      string_static("Unsupported Tga image type, only 'TrueColor' is supported (no rle)"),
      string_static("Unsupported Tga bits-per-pixel, only 32 bits (RGBA is supported)"),
      string_static("Unsupported Tga attribute depth, only 8 bit Tga alpha is supported"),
      string_static("Unsupported Tga image origin, only 'BottomLeft' is supported"),
      string_static("Interleaved Tga images are not supported"),
      string_static("Image dimensions need to be 4 pixel aligned"),
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
  File*              handle;
} BcuImage;

static BcuResult bcu_image_read(const String path, BcuImage* out) {
  BcuResult  error;
  File*      fileHandle = null;
  FileResult fileRes;
  if ((fileRes = file_create(g_alloc_heap, path, FileMode_Open, FileAccess_Read, &fileHandle))) {
    error = BcuResult_FileOpenFailed;
    goto Failure;
  }
  Mem data;
  if ((fileRes = file_map(fileHandle, &data))) {
    error = BcuResult_FileOpenFailed;
    goto Failure;
  }
  if (data.size < 18) {
    error = BcuResult_TgaFileTruncated;
    goto Failure;
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
    error = BcuResult_TgaUnsupportedColorMap;
    goto Failure;
  }
  if (imageType != 2 /* TrueColor */) {
    error = BcuResult_TgaUnsupportedImageType;
    goto Failure;
  }
  if (bitsPerPixel != 32) {
    error = BcuResult_TgaUnsupportedBitsPerPixel;
    goto Failure;
  }
  if (imageAttributeDepth != 8) {
    error = BcuResult_TgaUnsupportedAttributeDepth;
    goto Failure;
  }
  if (imageOrigin != 0 /* LowerLeft */) {
    error = BcuResult_TgaUnsupportedImageOrigin;
    goto Failure;
  }
  if (imageInterleave != 0 /* None */) {
    error = BcuResult_TgaUnsupportedInterleavedImage;
    goto Failure;
  }
  if (!bits_aligned(size.width, 4) || !bits_aligned(size.height, 4)) {
    error = BcuResult_ImageSizeNotAligned;
    goto Failure;
  }
  if (data.size < (size.width * size.height * sizeof(BcColor8888))) {
    error = BcuResult_TgaFileTruncated;
    goto Failure;
  }
  *out = (BcuImage){.size = size, .pixels = data.ptr, .handle = fileHandle};
  return BcuResult_Success;

Failure:
  if (fileHandle) {
    file_destroy(fileHandle);
  }
  return error;
}

static void bcu_image_close(BcuImage* image) {
  if (image->handle) {
    file_destroy(image->handle);
  }
}

static BcuResult bcu_image_write(const BcuSize size, const BcColor8888* pixels, const String path) {
  const usize headerSize    = 18;
  const usize pixelDataSize = size.width * size.height * sizeof(BcColor8888);
  const Mem   data          = alloc_alloc(g_alloc_heap, headerSize + pixelDataSize, 1);
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
  buffer     = mem_write_u8(buffer, 0);                 // imageSpecDescriptor.
  mem_cpy(buffer, mem_create(pixels, pixelDataSize));   // pixel data.

  String pathWithExt;
  if (string_eq(path_extension(path), string_lit("tga"))) {
    pathWithExt = path;
  } else {
    pathWithExt = fmt_write_scratch("{}.tga", fmt_path(path));
  }

  const FileResult writeRes = file_write_to_path_sync(pathWithExt, data);
  alloc_free(g_alloc_heap, data);
  return writeRes ? BcuResult_FileWriteFailed : BcuResult_Success;
}

static BcuResult bcu_run(const BcuImage* input, const String outputPath) {
  BcuResult result;
  if ((result = bcu_image_write(input->size, input->pixels, outputPath))) {
    return result;
  }

  log_i("Wrote output image", log_param("path", fmt_text(outputPath)));
  return BcuResult_Success;
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

  BcuImage  input = {0};
  BcuResult result;

  if ((result = bcu_image_read(inputPath, &input))) {
    log_e("Input image unsupported", log_param("error", fmt_text(bcu_result_str(result))));
    goto End;
  }

  const usize pixelCount    = input.size.width * input.size.height;
  const usize pixelDataSize = pixelCount * sizeof(BcColor8888);
  log_i(
      "Opened input image",
      log_param("path", fmt_text(inputPath)),
      log_param("width", fmt_int(input.size.width)),
      log_param("height", fmt_int(input.size.height)),
      log_param("pixels", fmt_int(pixelCount)),
      log_param("data", fmt_size(pixelDataSize)));

  result = bcu_run(&input, outputPath);

End:
  bcu_image_close(&input);
  return result == BcuResult_Success ? 0 : 1;
}
