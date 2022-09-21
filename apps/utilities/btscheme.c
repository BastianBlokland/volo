#include "app_cli.h"
#include "asset_behavior.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_path.h"
#include "log.h"

/**
 * BtScheme - Utility to generate a treescheme for the behavior file format.
 * The treescheme format is used by the 'https://www.bastian.tech/tree/' tree editor.
 * Format: https://github.com/BastianBlokland/typedtree-editor#example-of-the-scheme-format
 */

#define btscheme_default_path "ai.btscheme"

static bool btscheme_write(const String path) {
  DynString dynString = dynstring_create(g_alloc_heap, 64 * usize_kibibyte);

  asset_behavior_scheme_write(&dynString);

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
  cli_app_register_desc(app, string_lit("Utility to generate a behavior-tree scheme file."));

  g_outFlag = cli_register_flag(app, 'o', string_lit("out"), CliOptionFlags_Value);
  cli_register_desc(
      app, g_outFlag, string_lit("Output path (Default: '" btscheme_default_path "')."));

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

  const String outPathRaw = cli_read_string(invoc, g_outFlag, string_lit(btscheme_default_path));
  const String outPath    = path_build_scratch(outPathRaw);

  log_i("Generating behavior-tree scheme file", log_param("path", fmt_path(outPath)));

  return btscheme_write(outPath) ? 0 : 1;
}
