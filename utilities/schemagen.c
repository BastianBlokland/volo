#include "app/cli.h"
#include "asset/atlas.h"
#include "asset/data.h"
#include "asset/decal.h"
#include "asset/fonttex.h"
#include "asset/graphic.h"
#include "asset/icon.h"
#include "asset/inputmap.h"
#include "asset/level.h"
#include "asset/locale.h"
#include "asset/mesh.h"
#include "asset/prefab.h"
#include "asset/product.h"
#include "asset/script.h"
#include "asset/terrain.h"
#include "asset/texture.h"
#include "asset/vfx.h"
#include "asset/weapon.h"
#include "cli/app.h"
#include "cli/parse.h"
#include "cli/read.h"
#include "core/alloc.h"
#include "core/array.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/path.h"
#include "data/schema.h"
#include "log/logger.h"
#include "log/sink_json.h"
#include "log/sink_pretty.h"
#include "script/binder.h"

/**
 * SchemaGenerator - Utility to generate schema's for various asset formats used in Volo.
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
    {.name = string_static("locale.schema.json"),                .context = &g_assetLocaleDefMeta,             .writer = schema_writer_data   },
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

static CliId g_optDir;

AppType app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Utility to generate schema files."));

  g_optDir = cli_register_arg(app, string_lit("dir"), CliOptionFlags_Required);
  cli_register_desc(app, g_optDir, string_lit("Output directory."));

  return AppType_Console;
}

i32 app_cli_run(MAYBE_UNUSED const CliApp* app, const CliInvocation* invoc) {
  asset_data_init(true /* devSupport */);

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, g_fileStdOut, ~LogMask_Debug));
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
