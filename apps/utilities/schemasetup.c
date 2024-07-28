#include "app_cli.h"
#include "asset.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_path.h"
#include "data_schema.h"
#include "log.h"

/**
 * SchemaSetup - Utility to generate schema's for various asset formats used in Volo.
 *
 * Types of schemas:
 * - JsonSchema:   Validation schema supported for all of the json asset types.
 *                 https://json-schema.org/specification.html
 * - ScriptBinder: Used for script Ide support.
 */

typedef void (*SchemaWriter)(DynString*);

typedef struct {
  String       pattern;
  SchemaWriter writer;
} SchemaConfig;

// clang-format off
static const SchemaConfig g_schemaConfigs[] = {
    {.pattern = string_static("arraytex.schema.json"), .writer = asset_texture_array_jsonschema_write, },
    {.pattern = string_static("atlas.schema.json"),    .writer = asset_atlas_jsonschema_write,         },
    {.pattern = string_static("cursor.schema.json"),   .writer = asset_cursor_jsonschema_write,         },
    {.pattern = string_static("decal.schema.json"),    .writer = asset_decal_jsonschema_write,         },
    {.pattern = string_static("fonttex.schema.json"),  .writer = asset_fonttex_jsonschema_write,       },
    {.pattern = string_static("graphic.schema.json"),  .writer = asset_graphic_jsonschema_write,       },
    {.pattern = string_static("inputs.schema.json"),   .writer = asset_inputmap_jsonschema_write,      },
    {.pattern = string_static("level.schema.json"),    .writer = asset_level_jsonschema_write,         },
    {.pattern = string_static("prefabs.schema.json"),  .writer = asset_prefab_jsonschema_write,        },
    {.pattern = string_static("procmesh.schema.json"), .writer = asset_mesh_proc_jsonschema_write,     },
    {.pattern = string_static("proctex.schema.json"),  .writer = asset_texture_proc_jsonschema_write,  },
    {.pattern = string_static("products.schema.json"), .writer = asset_product_jsonschema_write,       },
    {.pattern = string_static("terrain.schema.json"),  .writer = asset_terrain_jsonschema_write,       },
    {.pattern = string_static("vfx.schema.json"),      .writer = asset_vfx_jsonschema_write,           },
    {.pattern = string_static("weapons.schema.json"),  .writer = asset_weapon_jsonschema_write,        },
    {.pattern = string_static("script_binder.json"),   .writer = asset_script_binder_write,            },
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
  DynString dynString = dynstring_create(g_allocHeap, 64 * usize_kibibyte);

  config->writer(&dynString);

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

static CliId g_optOut, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Utility to generate schema files."));

  g_optOut = cli_register_flag(app, 'o', string_lit("out"), CliOptionFlags_RequiredMultiValue);
  cli_register_desc(app, g_optOut, string_lit("Output paths."));
  cli_register_validator(app, g_optOut, scheme_validate_path);

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optOut);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  asset_data_init();

  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, ~LogMask_Debug));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  const CliParseValues outPathsRaw = cli_parse_values(invoc, g_optOut);
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
