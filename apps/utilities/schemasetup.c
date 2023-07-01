#include "app_cli.h"
#include "asset_behavior.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_path.h"
#include "data_schema.h"
#include "log.h"

/**
 * SchemaSetup - Utility to generate schema's for various asset formats used in Volo.
 *
 * Types of schemas:
 * - JsonSchema: Validation schema supported for all of the json asset types.
 *               https://json-schema.org/specification.html
 * - TreeSchema: Used by the 'https://www.bastian.tech/tree/' tree editor.
 *               https://github.com/BastianBlokland/typedtree-editor#example-of-the-scheme-format
 */

typedef AssetDataReg (*SchemaDataRegLookup)();
typedef void (*SchemaWriter)(const DataReg*, DynString*, DataMeta);

typedef struct {
  String              pattern;
  SchemaDataRegLookup source;
  SchemaWriter        writer;
} SchemaConfig;

// clang-format off
static const SchemaConfig g_schemaConfigs[] = {
    {.pattern = string_static("ai.btschema"),      .source = asset_behavior_datareg, .writer = data_treeschema_write},
    {.pattern = string_static("ai.schema.json"),  .source = asset_behavior_datareg, .writer = data_jsonschema_write},
};
// clang-format on

static const SchemaConfig* scheme_for_path(const String path) {
  const String fileName = path_filename(path);
  array_for_t(g_schemaConfigs, SchemaConfig, config) {
    if (string_match_glob(fileName, config->pattern, StringMatchFlags_IgnoreCase)) {
      return config;
    }
  }
  return null;
}

bool scheme_validate_path(const String input) { return scheme_for_path(input) != null; }

static bool schema_write(const SchemaConfig* config, const String path) {
  DynString dynString = dynstring_create(g_alloc_heap, 64 * usize_kibibyte);

  const AssetDataReg dataReg = config->source();
  config->writer(dataReg.registry, &dynString, dataReg.typeMeta);

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
  cli_register_validator(app, g_outFlag, scheme_validate_path);

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

  const SchemaConfig* config = scheme_for_path(outPath);
  if (!config) {
    log_e("Unable to determine schema type", log_param("path", fmt_path(outPath)));
    return 1;
  }

  log_i("Generating schema file", log_param("path", fmt_path(outPath)));

  return schema_write(config, outPath) ? 0 : 1;
}
