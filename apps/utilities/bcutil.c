#include "app_cli.h"
#include "core_alloc.h"
#include "core_file.h"
#include "log.h"

/**
 * BcUtil - Utility to test texture block compression.
 */

static bool bcutil_run(const String inputPath, const String outputPath) {
  log_i(
      "BcUtil run",
      log_param("input", fmt_path(inputPath)),
      log_param("output", fmt_path(outputPath)));

  File*      inFile;
  FileResult inRes;
  if ((inRes = file_create(g_alloc_heap, inputPath, FileMode_Open, FileAccess_Read, &inFile))) {
    log_e("Failed to open input file", log_param("path", fmt_path(inputPath)));
    return false;
  }
  String inData;
  if ((inRes = file_map(inFile, &inData))) {
    log_e("Failed to map input file", log_param("path", fmt_path(inputPath)));
    file_destroy(inFile);
    return false;
  }

  (void)outputPath;

  file_destroy(inFile);
  return true;
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
