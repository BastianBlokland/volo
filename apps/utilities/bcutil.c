#include "app_cli.h"
#include "core_alloc.h"
#include "core_bc.h"
#include "core_file.h"
#include "log.h"

/**
 * BcUtil - Utility to test texture block compression.
 *
 * NOTE: Contains an extremely simplistic tga parser that only supports uncompressed RGBA data which
 * uses lower-left as the image origin.
 */

typedef struct {
  u16 width, height;
} TgaHeader;

static Mem tga_header_read(Mem input, TgaHeader* out) {
  if (UNLIKELY(input.size < 18)) {
    return *out = (TgaHeader){0}, input; // Malformed header.
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
    log_e("Unsupported tga color-map type", log_param("type", fmt_int(colorMapType)));
    return *out = (TgaHeader){0}, input; // Unsupported color-map type.
  }
  if (imageType != 2 /* TrueColor */) {
    log_e("Unsupported tga image type", log_param("type", fmt_int(imageType)));
    return *out = (TgaHeader){0}, input; // Unsupported color-map type.
  }
  if (bitsPerPixel != 32) {
    log_e("Unsupported tga bitsPerPixel", log_param("bitsPerPixel", fmt_int(bitsPerPixel)));
    return *out = (TgaHeader){0}, input; // Unsupported image depth.
  }
  if (imageAttributeDepth != 8) {
    log_e("Unsupported tga image attribute depth");
    return *out = (TgaHeader){0}, input; // Unsupported alpha depth.
  }
  if (imageOrigin != 0 /* LowerLeft */) {
    log_e("Unsupported tga image origin");
    return *out = (TgaHeader){0}, input; // Unsupported image origin.
  }
  if (imageInterleave != 0 /* None */) {
    log_e("Unsupported tga interleaved image");
    return *out = (TgaHeader){0}, input; // Unsupported interleave mode.
  }

  *out = (TgaHeader){.width = width, .height = height};
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
  inData = tga_header_read(inData, &header);
  if (!header.width || !header.height) {
    log_e("Unsupported input tga file", log_param("path", fmt_path(inputPath)));
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
