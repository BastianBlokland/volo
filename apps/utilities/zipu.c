#include "app_cli.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_validate.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_gzip.h"
#include "core_path.h"
#include "core_zlib.h"
#include "log_logger.h"
#include "log_sink_json.h"
#include "log_sink_pretty.h"

/**
 * ZipUtility - Utility to test gzip/zlib decoding.
 */

static i32 zipu_decompress_data_gzip(const String data, const String path) {
  DynString outputBuffer = dynstring_create(g_allocHeap, usize_kibibyte);
  i32       res          = 0;

  GzipMeta  gzipMeta;
  GzipError gzipError;
  gzip_decode(data, &gzipMeta, &outputBuffer, &gzipError);
  if (gzipError) {
    log_e(
        "Failed to decode gzip data",
        log_param("path", fmt_path(path)),
        log_param("error", fmt_text(gzip_error_str(gzipError))));
    res = 2;
    goto Ret;
  }

  String outputPath;
  if (!string_is_empty(gzipMeta.name)) {
    outputPath = gzipMeta.name;
  } else {
    outputPath = fmt_write_scratch("{}.out", fmt_text(path_stem(path)));
  }

  const String outputDir = path_parent(path);
  if (!string_is_empty(outputDir)) {
    outputPath = path_build_scratch(outputDir, outputPath);
  }

  FileResult fileRes;
  if ((fileRes = file_write_to_path_atomic(outputPath, dynstring_view(&outputBuffer)))) {
    log_e(
        "Failed to write output file",
        log_param("path", fmt_path(outputPath)),
        log_param("error", fmt_text(file_result_str(fileRes))));
    res = 1;
    goto Ret;
  }

  log_i("Successfully decompressed file", log_param("path", fmt_path(outputPath)));

Ret:
  dynstring_destroy(&outputBuffer);
  return res;
}

static i32 zipu_decompress_data_zlib(const String data, const String path) {
  DynString outputBuffer = dynstring_create(g_allocHeap, usize_kibibyte);
  i32       res          = 0;

  ZlibError zlibError;
  zlib_decode(data, &outputBuffer, &zlibError);
  if (zlibError) {
    log_e(
        "Failed to decode zlib data",
        log_param("path", fmt_path(path)),
        log_param("error", fmt_text(zlib_error_str(zlibError))));
    res = 2;
    goto Ret;
  }

  String       outputPath = fmt_write_scratch("{}.out", fmt_text(path_stem(path)));
  const String outputDir  = path_parent(path);
  if (!string_is_empty(outputDir)) {
    outputPath = path_build_scratch(outputDir, outputPath);
  }

  FileResult fileRes;
  if ((fileRes = file_write_to_path_atomic(outputPath, dynstring_view(&outputBuffer)))) {
    log_e(
        "Failed to write output file",
        log_param("path", fmt_path(outputPath)),
        log_param("error", fmt_text(file_result_str(fileRes))));
    res = 1;
    goto Ret;
  }

  log_i("Successfully decompressed file", log_param("path", fmt_path(outputPath)));

Ret:
  dynstring_destroy(&outputBuffer);
  return res;
}

static i32 zipu_decompress_data(const String data, const String path) {
  const String extension = path_extension(path);
  if (string_eq(extension, string_lit("gz"))) {
    return zipu_decompress_data_gzip(data, path);
  }
  if (string_eq(extension, string_lit("zz"))) {
    return zipu_decompress_data_zlib(data, path);
  }
  log_e(
      "Unsupported data extension",
      log_param("path", fmt_path(path)),
      log_param("extension", fmt_text(extension)));
  return 3;
}

static i32 zipu_decompress(const String inputPath) {
  i32        res = 0;
  FileResult fileRes;

  File* inputFile = null;
  if ((fileRes = file_create(g_allocHeap, inputPath, FileMode_Open, FileAccess_Read, &inputFile))) {
    log_e(
        "Failed to open input file",
        log_param("path", fmt_path(inputPath)),
        log_param("error", fmt_text(file_result_str(fileRes))));
    res = 1;
    goto Ret;
  }

  String inputData;
  if ((fileRes = file_map(inputFile, &inputData, FileHints_Prefetch))) {
    log_e(
        "Failed to map input file",
        log_param("path", fmt_path(inputPath)),
        log_param("error", fmt_text(file_result_str(fileRes))));
    res = 1;
    goto Ret;
  }

  res = zipu_decompress_data(inputData, inputPath);

Ret:
  if (inputFile) {
    file_destroy(inputFile);
  }
  return res;
}

static CliId g_optFiles, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Zip Utility."));

  g_optFiles = cli_register_arg(app, string_lit("files"), CliOptionFlags_RequiredMultiValue);
  cli_register_desc(app, g_optFiles, string_lit("GZip (.gz) / ZLib (.zz) files to decompress."));
  cli_register_validator(app, g_optFiles, cli_validate_file_regular);

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optFiles);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, ~LogMask_Debug));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  const CliParseValues files = cli_parse_values(invoc, g_optFiles);
  for (usize i = 0; i != files.count; ++i) {
    const i32 res = zipu_decompress(files.values[i]);
    if (res) {
      return res;
    }
  }
  return 0;
}
