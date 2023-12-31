#include "app_cli.h"
#include "core_alloc.h"
#include "core_bc.h"
#include "core_bits.h"
#include "core_file.h"
#include "log.h"

/**
 * BcUtil - Utility to test texture block compression.
 *
 * NOTE: Contains an extremely simplistic tga parser that only supports uncompressed RGBA data which
 * uses lower-left as the image origin.
 */

typedef enum {
  Result_Success = 0,
  Result_TgaMalformedHeader,
  Result_TgaUnsupportedColorMap,
  Result_TgaUnsupportedImageType,
  Result_TgaUnsupportedBitsPerPixel,
  Result_TgaUnsupportedAttributeDepth,
  Result_TgaUnsupportedImageOrigin,
  Result_TgaUnsupportedInterleavedImage,

  Result_Count,
} Result;

static String result_str(const Result res) {
  static const String g_msgs[] = {
      string_static("Success"),
      string_static("Malformed Tga header"),
      string_static("Color-mapped Tga images are not supported"),
      string_static("Unsupported Tga image type, only 'TrueColor' is supported (no rle)"),
      string_static("Unsupported Tga bits-per-pixel, only 32 bits (RGBA is supported)"),
      string_static("Unsupported Tga attribute depth, only 8 bit Tga alpha is supported"),
      string_static("Unsupported Tga image origin, only 'BottomLeft' is supported"),
      string_static("Interleaved Tga images are not supported"),
  };
  ASSERT(array_elems(g_msgs) == Result_Count, "Incorrect number of result messages");
  return g_msgs[res];
}

typedef struct {
  u16 width, height;
} TgaHeader;

static Mem tga_header_read(Mem input, TgaHeader* out, Result* res) {
  if (UNLIKELY(input.size < 18)) {
    return *res = Result_TgaMalformedHeader, input;
  }
  u8  colorMapType, imageType, bitsPerPixel, imageSpecDescriptorRaw;
  u16 width, height;

  input = mem_consume(input, 1); // Skip over 'idLength'.
  input = mem_consume_u8(input, &colorMapType);
  input = mem_consume_u8(input, &imageType);
  input = mem_consume(input, 5); // Skip over 'ColorMapSpec'.
  input = mem_consume(input, 4); // Skip over 'origin'.
  input = mem_consume_le_u16(input, &width);
  input = mem_consume_le_u16(input, &height);
  input = mem_consume_u8(input, &bitsPerPixel);
  input = mem_consume_u8(input, &imageSpecDescriptorRaw);

  const u8 imageAttributeDepth = imageSpecDescriptorRaw & u8_lit(0b1111);
  const u8 imageOrigin         = imageSpecDescriptorRaw & u8_lit(0b110000);
  const u8 imageInterleave     = imageSpecDescriptorRaw & u8_lit(0b11000000);

  if (colorMapType != 0 /* Absent*/) {
    return *res = Result_TgaUnsupportedColorMap, input;
  }
  if (imageType != 2 /* TrueColor */) {
    return *res = Result_TgaUnsupportedImageType, input;
  }
  if (bitsPerPixel != 32) {
    return *res = Result_TgaUnsupportedBitsPerPixel, input;
  }
  if (imageAttributeDepth != 8) {
    return *res = Result_TgaUnsupportedAttributeDepth, input;
  }
  if (imageOrigin != 0 /* LowerLeft */) {
    return *res = Result_TgaUnsupportedImageOrigin, input;
  }
  if (imageInterleave != 0 /* None */) {
    return *res = Result_TgaUnsupportedInterleavedImage, input;
  }

  *out = (TgaHeader){.width = width, .height = height};
  *res = Result_Success;
  return input;
}

static void tga_header_write(const TgaHeader* header, DynString* out) {
  Mem headerMem = dynarray_push(out, 18);
  headerMem     = mem_write_u8_zero(headerMem, 2);            // 'idLength' and 'colorMapType'.
  headerMem     = mem_write_u8(headerMem, 2 /* TrueColor */); // 'imageType'.
  headerMem     = mem_write_u8_zero(headerMem, 9);            // 'colorMapSpec' and 'origin'.
  headerMem     = mem_write_le_u16(headerMem, header->width);
  headerMem     = mem_write_le_u16(headerMem, header->height);
  headerMem     = mem_write_u8(headerMem, 32);       // 'bitsPerPixel'.
  headerMem     = mem_write_u8(headerMem, 0b100000); // 'imageSpecDescriptor'.
}

static bool bcutil_run(const String inputPath, const String outputPath) {
  bool success = false;

  log_i(
      "BcUtil run",
      log_param("input", fmt_path(inputPath)),
      log_param("output", fmt_path(outputPath)));

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
  TgaHeader header;
  Result    headerResult;
  inData = tga_header_read(inData, &header, &headerResult);
  if (headerResult != Result_Success) {
    log_e("Unsupported input tga file", log_param("error", fmt_text(result_str(headerResult))));
    goto End;
  }
  if (!bits_ispow2(header.width) || !bits_ispow2(header.height)) {
    log_e("Input tga image dimensions needs to be a power of two");
    goto End;
  }
  if (header.width < 4 || header.height < 4) {
    log_e("Input tga image dimensions too small (needs to be at least 4 pixels)");
    goto End;
  }

  // const u32          pixelCount = header.width * header.height;
  // const BcColor8888* pixels     = inData.ptr;
  (void)tga_header_write;

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

  return bcutil_run(inputPath, outputPath) ? 0 : 1;
}
