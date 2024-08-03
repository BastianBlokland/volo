#include "app_cli.h"
#include "asset.h"
#include "core_file.h"

/**
 * Blob2J - Utility to convert Volo binary blobs to json.
 */

static CliId g_optPath, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Utility to convert Volo binary blobs to json."));

  g_optPath = cli_register_flag(app, 'p', string_lit("path"), CliOptionFlags_Required);
  cli_register_desc(app, g_optPath, string_lit("Path to the binary blob."));
  cli_register_validator(app, g_optPath, cli_validate_file_regular);

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optPath);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  asset_data_init();

  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }

  return 0;
}
