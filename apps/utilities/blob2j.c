#include "app_cli.h"
#include "asset_data.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "cli_validate.h"
#include "core_alloc.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "core_format.h"
#include "data_read.h"
#include "data_utils.h"
#include "data_write.h"

/**
 * Blob2J - Utility to convert Volo binary blobs to json.
 */

typedef struct {
  bool noTrail; // Disallow trailing data.
  u64  offset;
} Blob2jConfig;

static i32 blob2j_run(const Blob2jConfig* cfg, File* inputFile, File* outputFile) {
  DynString buff     = dynstring_create(g_allocHeap, usize_kibibyte);
  Mem       data     = mem_empty;
  i32       exitCode = 0;

  if (file_read_to_end_sync(inputFile, &buff)) {
    file_write_sync(g_fileStdErr, string_lit("ERROR: Failed to read input.\n"));
    exitCode = 1;
    goto Ret;
  }
  const String input = string_slice(dynstring_view(&buff), cfg->offset, buff.size - cfg->offset);

  DataBinHeader  dataHeader;
  DataReadResult readRes;
  data_read_bin_header(input, &dataHeader, &readRes);
  if (readRes.error) {
    file_write_sync(
        g_fileStdErr,
        fmt_write_scratch("ERROR: Failed to read input: {}.\n", fmt_text(readRes.errorMsg)));
    exitCode = 1;
    goto Ret;
  }

  const DataMeta dataMeta = {
      .type      = data_type_from_name_hash(g_dataReg, dataHeader.metaTypeNameHash),
      .container = dataHeader.metaContainer,
      .flags     = dataHeader.metaFlags,
  };
  if (!dataMeta.type) {
    file_write_sync(g_fileStdErr, string_lit("ERROR: Unknown input type.\n"));
    exitCode = 1;
    goto Ret;
  }

  const usize dataSize  = data_meta_size(g_dataReg, dataMeta);
  const usize dataAlign = data_meta_align(g_dataReg, dataMeta);
  data                  = alloc_alloc(g_allocHeap, dataSize, dataAlign);

  const String inputRem = data_read_bin(g_dataReg, input, g_allocHeap, dataMeta, data, &readRes);
  if (readRes.error) {
    file_write_sync(
        g_fileStdErr,
        fmt_write_scratch("ERROR: Failed to read input: {}.\n", fmt_text(readRes.errorMsg)));
    exitCode = 1;
    goto Ret;
  }
  if (cfg->noTrail && !string_is_empty(inputRem)) {
    file_write_sync(g_fileStdErr, fmt_write_scratch("ERROR: Unexpected input data after blob.\n"));
    data_destroy(g_dataReg, g_allocHeap, dataMeta, data);
    exitCode = 1;
    goto Ret;
  }

  dynstring_clear(&buff);
  data_write_json(g_dataReg, &buff, dataMeta, data, &data_write_json_opts(.compact = true));
  dynstring_append_char(&buff, '\n');

  data_destroy(g_dataReg, g_allocHeap, dataMeta, data);

  if (file_write_sync(outputFile, dynstring_view(&buff))) {
    file_write_sync(g_fileStdErr, string_lit("ERROR: Failed to write output.\n"));
    exitCode = 1;
    goto Ret;
  }

Ret:
  alloc_maybe_free(g_allocHeap, data);
  dynstring_destroy(&buff);
  return exitCode;
}

static CliId g_optPath, g_optOffset, g_optNoTrail, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Utility to convert Volo binary blobs to json."));

  g_optPath = cli_register_arg(app, string_lit("path"), CliOptionFlags_Value);
  cli_register_desc(app, g_optPath, string_lit("Path to the binary blob."));
  cli_register_validator(app, g_optPath, cli_validate_file_regular);

  g_optOffset = cli_register_flag(app, 'o', string_lit("offset"), CliOptionFlags_Value);
  cli_register_desc(app, g_optOffset, string_lit("Offset to read at."));

  g_optNoTrail = cli_register_flag(app, '\0', string_lit("notrail"), CliOptionFlags_None);
  cli_register_desc(app, g_optNoTrail, string_lit("Disallow trailing data."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optPath);
  cli_register_exclusions(app, g_optHelp, g_optOffset);
  cli_register_exclusions(app, g_optHelp, g_optNoTrail);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  asset_data_init();

  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }

  const Blob2jConfig cfg = {
      .noTrail = cli_parse_provided(invoc, g_optNoTrail),
      .offset  = cli_read_u64(invoc, g_optOffset, 0),
  };

  File* inputFile      = null;
  bool  inputFileOwned = false;

  const String inputPath = cli_read_string(invoc, g_optPath, string_empty);
  if (string_is_empty(inputPath)) {
    if (tty_isatty(g_fileStdIn)) {
      file_write_sync(g_fileStdErr, string_lit("ERROR: Input blob expected (path or stdin).\n"));
      return 1;
    }
    inputFile = g_fileStdIn;
  } else {
    if (file_create(g_allocHeap, inputPath, FileMode_Open, FileAccess_Read, &inputFile)) {
      file_write_sync(g_fileStdErr, string_lit("ERROR: Failed to open input file.\n"));
      return 1;
    }
    inputFileOwned = true;
  }

  const i32 ret = blob2j_run(&cfg, inputFile, g_fileStdOut);
  if (inputFileOwned) {
    file_destroy(inputFile);
  }
  return ret;
}
