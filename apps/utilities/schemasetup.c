#include "app_cli.h"
#include "asset_atlas.h"
#include "asset_data.h"
#include "asset_decal.h"
#include "asset_fonttex.h"
#include "asset_graphic.h"
#include "asset_icon.h"
#include "asset_inputmap.h"
#include "asset_level.h"
#include "asset_mesh.h"
#include "asset_prefab.h"
#include "asset_product.h"
#include "asset_script.h"
#include "asset_terrain.h"
#include "asset_texture.h"
#include "asset_vfx.h"
#include "asset_weapon.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "core_path.h"
#include "data_schema.h"
#include "log_logger.h"
#include "log_sink_json.h"
#include "log_sink_pretty.h"
#include "script_binder.h"

/**
 * SchemaSetup - Utility to generate schema's for various asset formats used in Volo.
 *
 * Types of schemas:
 * - JsonSchema:   Validation schema supported for all of the json asset types.
 *                 https://json-schema.org/specification.html
 * - ScriptBinder: Used for script ide support.
 */

typedef void (*SchemaWriter)(DynString*, const void* context);

typedef struct {
  String       name;
  SchemaWriter writer;
  const void*  context;
} SchemaConfig;

static void schema_writer_data(DynString* str, const void* context) {
  const DataMeta* typeMeta = context;

  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, *typeMeta, schemaFlags);
}

static void schema_writer_script(DynString* str, const void* context) {
  const ScriptBinder* const* binder = context;

  script_binder_write(str, *binder);
}

// clang-format off
static const SchemaConfig g_schemaConfigs[] = {
    {.name = string_static("arraytex.schema.json"),              .context = &g_assetTexArrayDefMeta,           .writer = schema_writer_data   },
    {.name = string_static("atlas.schema.json"),                 .context = &g_assetAtlasDefMeta,              .writer = schema_writer_data   },
    {.name = string_static("decal.schema.json"),                 .context = &g_assetDecalDefMeta,              .writer = schema_writer_data   },
    {.name = string_static("fonttex.schema.json"),               .context = &g_assetFontTexDefMeta,            .writer = schema_writer_data   },
    {.name = string_static("graphic.schema.json"),               .context = &g_assetGraphicDefMeta,            .writer = schema_writer_data   },
    {.name = string_static("icon.schema.json"),                  .context = &g_assetIconDefMeta,               .writer = schema_writer_data   },
    {.name = string_static("inputs.schema.json"),                .context = &g_assetInputDefMeta,              .writer = schema_writer_data   },
    {.name = string_static("level.schema.json"),                 .context = &g_assetLevelDefMeta,              .writer = schema_writer_data   },
    {.name = string_static("prefabs.schema.json"),               .context = &g_assetPrefabDefMeta,             .writer = schema_writer_data   },
    {.name = string_static("procmesh.schema.json"),              .context = &g_assetProcMeshDefMeta,           .writer = schema_writer_data   },
    {.name = string_static("proctex.schema.json"),               .context = &g_assetTexProcDefMeta,            .writer = schema_writer_data   },
    {.name = string_static("products.schema.json"),              .context = &g_assetProductDefMeta,            .writer = schema_writer_data   },
    {.name = string_static("terrain.schema.json"),               .context = &g_assetTerrainDefMeta,            .writer = schema_writer_data   },
    {.name = string_static("vfx.schema.json"),                   .context = &g_assetVfxDefMeta,                .writer = schema_writer_data   },
    {.name = string_static("weapons.schema.json"),               .context = &g_assetWeaponDefMeta,             .writer = schema_writer_data   },
    {.name = string_static("script_import_mesh_binder.json"),    .context = &g_assetScriptImportMeshBinder,    .writer = schema_writer_script },
    {.name = string_static("script_import_texture_binder.json"), .context = &g_assetScriptImportTextureBinder, .writer = schema_writer_script },
    {.name = string_static("script_scene_binder.json"),          .context = &g_assetScriptSceneBinder,         .writer = schema_writer_script },
};
// clang-format on

static bool schema_write(const SchemaConfig* config, const String outDir) {
  const String outPath   = path_build_scratch(outDir, config->name);
  DynString    dynString = dynstring_create(g_allocHeap, 64 * usize_kibibyte);

  config->writer(&dynString, config->context);

  FileResult res;
  if ((res = file_write_to_path_atomic(outPath, dynstring_view(&dynString)))) {
    log_e(
        "Failed to write output file",
        log_param("err", fmt_text(file_result_str(res))),
        log_param("path", fmt_path(outPath)));
  }

  dynstring_destroy(&dynString);
  return res == FileResult_Success;
}

static CliId g_optDir, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Utility to generate schema files."));

  g_optDir = cli_register_arg(app, string_lit("dir"), CliOptionFlags_Required);
  cli_register_desc(app, g_optDir, string_lit("Output directory."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optDir);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  asset_data_init();

  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, ~LogMask_Debug));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  const String outDir = cli_read_string(invoc, g_optDir, string_empty);
  if (string_is_empty(outDir)) {
    log_e("Output directory missing");
    return 1;
  }
  file_create_dir_sync(outDir);

  for (u32 i = 0; i != array_elems(g_schemaConfigs); ++i) {
    const SchemaConfig* config = &g_schemaConfigs[i];

    log_i("Generating schema file", log_param("file", fmt_text(config->name)));

    if (!schema_write(config, outDir)) {
      return 1;
    }
  }

  return 0;
}
