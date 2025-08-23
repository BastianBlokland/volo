#include "app/cli.h"
#include "asset/data.h"
#include "cli/app.h"
#include "cli/parse.h"
#include "cli/read.h"
#include "cli/validate.h"
#include "core/alloc.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/format.h"
#include "data/read.h"
#include "data/utils.h"
#include "data/write.h"

/**
 * Blob2J - Utility to convert Volo binary blobs to json.
 */

typedef struct {
  u64 offset;
} Blob2jConfig;

static i32 blob2j_run(const Blob2jConfig* cfg, File* inputFile, File* outputFile) {
  DynString buffer   = dynstring_create(g_allocHeap, 16 * usize_kibibyte);
  Mem       data     = mem_empty;
  i32       exitCode = 0;

  if (cfg->offset && file_skip_sync(inputFile, cfg->offset)) {
    file_write_sync(g_fileStdErr, string_lit("ERROR: Failed to skip input.\n"));
    exitCode = 1;
    goto Ret;
  }

  if (file_read_sync(inputFile, &buffer)) {
    file_write_sync(g_fileStdErr, string_lit("ERROR: Failed to read input.\n"));
    exitCode = 1;
    goto Ret;
  }

  DataBinHeader  dataHeader;
  DataReadResult readRes;
  data_read_bin_header(dynstring_view(&buffer), &dataHeader, &readRes);
  if (readRes.error) {
    file_write_sync(
        g_fileStdErr,
        fmt_write_scratch("ERROR: Failed to read input: {}.\n", fmt_text(readRes.errorMsg)));
    exitCode = 1;
    goto Ret;
  }

  if (dataHeader.size) {
    while (buffer.size < dataHeader.size) {
      if (file_read_sync(inputFile, &buffer)) {
        file_write_sync(g_fileStdErr, string_lit("ERROR: Failed to read input.\n"));
        exitCode = 1;
        goto Ret;
      }
    }
  } else {
    /**
     * NOTE: Old versions of the blob format did not store the size, to support them we read the
     * whole file.
     */
    if (file_read_to_end_sync(inputFile, &buffer)) {
      file_write_sync(g_fileStdErr, string_lit("ERROR: Failed to read input.\n"));
      exitCode = 1;
      goto Ret;
    }
  }

  const DataMeta dataMeta = {
      .type      = data_type_from_name_hash(g_dataReg, dataHeader.metaTypeNameHash),
      .container = dataHeader.metaContainer,
      .flags     = dataHeader.metaFlags,
  };
  if (!dataMeta.type) {
    file_write_sync(g_fileStdErr, string_lit("ERROR: Unsupported input type.\n"));
    exitCode = 1;
    goto Ret;
  }

  const usize dataSize  = data_meta_size(g_dataReg, dataMeta);
  const usize dataAlign = data_meta_align(g_dataReg, dataMeta);
  data                  = alloc_alloc(g_allocHeap, dataSize, dataAlign);

  data_read_bin(g_dataReg, dynstring_view(&buffer), g_allocHeap, dataMeta, data, &readRes);
  if (readRes.error) {
    file_write_sync(
        g_fileStdErr,
        fmt_write_scratch("ERROR: Failed to read input: {}.\n", fmt_text(readRes.errorMsg)));
    exitCode = 1;
    goto Ret;
  }

  dynstring_clear(&buffer);
  data_write_json(g_dataReg, &buffer, dataMeta, data, &data_write_json_opts(.compact = true));
  dynstring_append_char(&buffer, '\n');

  data_destroy(g_dataReg, g_allocHeap, dataMeta, data);

  if (file_write_sync(outputFile, dynstring_view(&buffer))) {
    file_write_sync(g_fileStdErr, string_lit("ERROR: Failed to write output.\n"));
    exitCode = 1;
    goto Ret;
  }

Ret:
  alloc_maybe_free(g_allocHeap, data);
  dynstring_destroy(&buffer);
  return exitCode;
}

static CliId g_optPath, g_optOffset;

AppType app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Utility to convert Volo binary blobs to json."));

  g_optPath = cli_register_arg(app, string_lit("path"), CliOptionFlags_Value);
  cli_register_desc(app, g_optPath, string_lit("Path to the binary blob."));
  cli_register_validator(app, g_optPath, cli_validate_file_regular);

  g_optOffset = cli_register_flag(app, 'o', string_lit("offset"), CliOptionFlags_Value);
  cli_register_desc(app, g_optOffset, string_lit("Offset to read at."));

  return AppType_Console;
}

i32 app_cli_run(MAYBE_UNUSED const CliApp* app, const CliInvocation* invoc) {
  asset_data_init();

  const Blob2jConfig cfg = {
      .offset = cli_read_u64(invoc, g_optOffset, 0),
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
