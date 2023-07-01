#include "app_cli.h"
#include "asset_behavior.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_path.h"
#include "log.h"

/**
 * SchemeSetup - Utility to generate schema's for various asset formats used in Volo.
 *
 * Types of schemes:
 * - JsonSchema: Validation scheme supported for all of the json asset types.
 *               https://json-schema.org/specification.html
 * - TreeSchema: Used by the 'https://www.bastian.tech/tree/' tree editor.
 *               https://github.com/BastianBlokland/typedtree-editor#example-of-the-scheme-format
 */

static bool btschema_write(const String path) {
  DynString dynString = dynstring_create(g_alloc_heap, 64 * usize_kibibyte);

  asset_behavior_schema_write(&dynString);

  FileResult res;
  if ((res = file_write_to_path_sync(path, dynstring_view(&dynString)))) {
    log_e(
        "Failed to write output file",
        log_param("err", fmt_text(file_result_str(res))),
        log_param("path", fmt_path(path)));
  }

  dynstring_destroy(&dynString);
  return res == FileResult_Success;
}

static CliId g_outFlag, g_helpFlag;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Utility to generate schema files."));

  g_outFlag = cli_register_flag(app, 'o', string_lit("out"), CliOptionFlags_Required);
  cli_register_desc(app, g_outFlag, string_lit("Output path."));

  g_helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_helpFlag, string_lit("Display this help page."));
  cli_register_exclusions(app, g_helpFlag, g_outFlag);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_helpFlag)) {
    cli_help_write_file(app, g_file_stdout);
    return 0;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_alloc_heap, ~LogMask_Debug));
  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  const String outPathRaw = cli_read_string(invoc, g_outFlag, string_empty);
  const String outPath    = path_build_scratch(outPathRaw);

  log_i("Generating schema file", log_param("path", fmt_path(outPath)));

  return btschema_write(outPath) ? 0 : 1;
}
