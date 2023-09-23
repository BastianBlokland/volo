#include "app_cli.h"
#include "asset_atlas.h"
#include "asset_behavior.h"
#include "asset_decal.h"
#include "asset_ftx.h"
#include "asset_graphic.h"
#include "asset_inputmap.h"
#include "asset_level.h"
#include "asset_mesh.h"
#include "asset_prefab.h"
#include "asset_product.h"
#include "asset_texture.h"
#include "asset_vfx.h"
#include "asset_weapon.h"
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

typedef AssetDataReg (*SchemaDataRegLookup)(void);
typedef void (*SchemaWriter)(const DataReg*, DynString*, DataMeta);

typedef struct {
  String              pattern;
  SchemaDataRegLookup source;
  SchemaWriter        writer;
} SchemaConfig;

// clang-format off
static const SchemaConfig g_schemaConfigs[] = {
    {.pattern = string_static("atlas.schema.json"),    .source = asset_atlas_datareg,         .writer = data_jsonschema_write},
    {.pattern = string_static("arraytex.schema.json"), .source = asset_texture_array_datareg, .writer = data_jsonschema_write},
    {.pattern = string_static("bt.btschema"),          .source = asset_behavior_datareg,      .writer = data_treeschema_write},
    {.pattern = string_static("bt.schema.json"),       .source = asset_behavior_datareg,      .writer = data_jsonschema_write},
    {.pattern = string_static("dcl.schema.json"),      .source = asset_decal_datareg,         .writer = data_jsonschema_write},
    {.pattern = string_static("ftx.schema.json"),      .source = asset_ftx_datareg,           .writer = data_jsonschema_write},
    {.pattern = string_static("graphic.schema.json"),  .source = asset_graphic_datareg,       .writer = data_jsonschema_write},
    {.pattern = string_static("inputs.schema.json"),   .source = asset_inputmap_datareg,      .writer = data_jsonschema_write},
    {.pattern = string_static("level.schema.json"),    .source = asset_level_datareg,         .writer = data_jsonschema_write},
    {.pattern = string_static("prefabs.schema.json"),  .source = asset_prefab_datareg,        .writer = data_jsonschema_write},
    {.pattern = string_static("pme.schema.json"),      .source = asset_mesh_pme_datareg,      .writer = data_jsonschema_write},
    {.pattern = string_static("ptx.schema.json"),      .source = asset_texture_ptx_datareg,   .writer = data_jsonschema_write},
    {.pattern = string_static("vfx.schema.json"),      .source = asset_vfx_datareg,           .writer = data_jsonschema_write},
    {.pattern = string_static("weapons.schema.json"),  .source = asset_weapon_datareg,        .writer = data_jsonschema_write},
    {.pattern = string_static("products.schema.json"), .source = asset_product_datareg,       .writer = data_jsonschema_write},
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

  g_outFlag = cli_register_flag(app, 'o', string_lit("out"), CliOptionFlags_RequiredMultiValue);
  cli_register_desc(app, g_outFlag, string_lit("Output paths."));
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

  const CliParseValues outPathsRaw = cli_parse_values(invoc, g_outFlag);
  for (u32 i = 0; i != outPathsRaw.count; ++i) {
    const String outPathRaw = outPathsRaw.values[i];
    const String outPath    = path_build_scratch(outPathRaw);

    const SchemaConfig* config = scheme_for_path(outPath);
    if (!config) {
      log_e("Unable to determine schema type", log_param("path", fmt_path(outPath)));
      return 1;
    }

    log_i("Generating schema file", log_param("path", fmt_path(outPath)));

    if (!schema_write(config, outPath)) {
      return 1;
    }
  }

  return 0;
}
