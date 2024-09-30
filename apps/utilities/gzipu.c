#include "app_cli.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_gzip.h"
#include "core_path.h"
#include "log.h"

/**
 * GZipUtility - Utility to test gzip decoding.
 */

static i32 gzipu_run(const String inputPath) {
  i32        res = 0;
  FileResult fileRes;

  const String outputDir    = path_parent(inputPath);
  DynString    outputBuffer = dynstring_create(g_allocHeap, usize_kibibyte);

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

  u32 outputCounter = 0;
  do {
    GzipMeta  gzipMeta;
    GzipError gzipError;
    inputData = gzip_decode(inputData, &gzipMeta, &outputBuffer, &gzipError);

    if (gzipError) {
      log_e(
          "Failed to decode GZip data",
          log_param("path", fmt_path(inputPath)),
          log_param("error", fmt_text(gzip_error_str(gzipError))));
      res = 2;
      goto Ret;
    }

    String outputPath;
    if (!string_is_empty(gzipMeta.name)) {
      outputPath = gzipMeta.name;
    } else if (!outputCounter) {
      outputPath = path_stem(inputPath);
    } else {
      outputPath =
          fmt_write_scratch("{}.{}", fmt_text(path_stem(inputPath)), fmt_int(outputCounter));
    }
    if (!string_is_empty(outputDir)) {
      outputPath = path_build_scratch(outputDir, outputPath);
    }

    if ((fileRes = file_write_to_path_atomic(outputPath, dynstring_view(&outputBuffer)))) {
      log_e(
          "Failed to write output file",
          log_param("path", fmt_path(outputPath)),
          log_param("error", fmt_text(file_result_str(fileRes))));
      res = 3;
      goto Ret;
    }
    dynstring_clear(&outputBuffer);

    log_i("Successfully decoded GZip file", log_param("path", fmt_path(outputPath)));

    ++outputCounter;
  } while (!string_is_empty(inputData));

Ret:
  if (inputFile) {
    file_destroy(inputFile);
  }
  dynstring_destroy(&outputBuffer);
  return res;
}

static CliId g_optInput, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("GZip Utility."));

  g_optInput = cli_register_arg(app, string_lit("input"), CliOptionFlags_Required);
  cli_register_desc(app, g_optInput, string_lit("Gzip (.gz) file path."));
  cli_register_validator(app, g_optInput, cli_validate_file_regular);

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optInput);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, ~LogMask_Debug));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  const String inputPath = cli_read_string(invoc, g_optInput, string_empty);

  return gzipu_run(inputPath);
}
