#include "app_cli.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_path.h"
#include "core_sort.h"
#include "json.h"
#include "log.h"

/**
 * DebugSetup - Utility to generate debugger configuration files for a set of executables.
 *
 * For example a VsCode launch config file:
 * ```
 * {
 *   "version": "0.2.0",
 *   "configurations": [
 *     {
 *       "name": "volo_check_test",
 *       "type": "lldb",
 *       "request": "launch",
 *       "program": "/home/user/dev/projects/volo/build/libs/check/volo_check_test",
 *       "cwd": "/home/user/dev/projects/volo/",
 *       "args": [],
 *       "terminal": "integrated",
 *       "stopOnEntry": false
 *     }
 *   ]
 * }
 * ```
 */

typedef enum {
  DbgSetupDbg_Lldb,
  DbgSetupDbg_Cppvsdbg,

  DbgSetupDbg_Count,
  DbgSetupDbg_Default = DbgSetupDbg_Lldb,
} DbgSetupDbg;

static const String g_dbgStrs[] = {
    string_static("lldb"),
    string_static("cppvsdbg"),
};
ASSERT(array_elems(g_dbgStrs) == DbgSetupDbg_Count, "Incorrect number of dbg strings");

static bool dbgsetup_validate_dbg(const String input) {
  array_for_t(g_dbgStrs, String, cfg) {
    if (string_eq(*cfg, input)) {
      return true;
    }
  }
  return false;
}

typedef struct {
  DbgSetupDbg dbg;
  String      workspace;
  String*     targets;
  usize       targetCount;
} DbgSetupCtx;

static bool dbgsetup_write_json(String path, const JsonDoc* jsonDoc, const JsonVal jsonVal) {
  DynString dynString = dynstring_create(g_allocHeap, 64 * usize_kibibyte);
  json_write(&dynString, jsonDoc, jsonVal, &json_write_opts(.mode = JsonWriteMode_Compact));

  FileResult res;
  if ((res = file_write_to_path_atomic(path, dynstring_view(&dynString)))) {
    log_e(
        "Failed to write output file",
        log_param("err", fmt_text(file_result_str(res))),
        log_param("path", fmt_path(path)));
  }

  dynstring_destroy(&dynString);
  return res == FileResult_Success;
}

static JsonVal dbgsetup_vscode_gen_launch_entry(DbgSetupCtx* ctx, JsonDoc* doc, String target) {
  const JsonVal obj = json_add_object(doc);
  json_add_field_lit(
      doc,
      obj,
      "name",
      json_add_string(doc, fmt_write_scratch("{} (Launch)", fmt_text(path_stem(target)))));
  json_add_field_lit(doc, obj, "type", json_add_string(doc, g_dbgStrs[ctx->dbg]));
  json_add_field_lit(doc, obj, "request", json_add_string_lit(doc, "launch"));
  json_add_field_lit(doc, obj, "program", json_add_string(doc, target));
  json_add_field_lit(doc, obj, "cwd", json_add_string(doc, ctx->workspace));
  json_add_field_lit(doc, obj, "args", json_add_array(doc));
  json_add_field_lit(doc, obj, "terminal", json_add_string_lit(doc, "integrated"));
  json_add_field_lit(doc, obj, "stopOnEntry", json_add_bool(doc, false));
  return obj;
}

static JsonVal dbgsetup_vscode_gen_attach_entry(DbgSetupCtx* ctx, JsonDoc* doc, String target) {
  const JsonVal obj = json_add_object(doc);
  json_add_field_lit(
      doc,
      obj,
      "name",
      json_add_string(doc, fmt_write_scratch("{} (Attach)", fmt_text(path_stem(target)))));
  json_add_field_lit(doc, obj, "type", json_add_string(doc, g_dbgStrs[ctx->dbg]));
  json_add_field_lit(doc, obj, "request", json_add_string_lit(doc, "attach"));
  json_add_field_lit(doc, obj, "program", json_add_string(doc, target));
  json_add_field_lit(doc, obj, "processId", json_add_string_lit(doc, "${command:pickProcess}"));
  return obj;
}

static JsonVal dbgsetup_vscode_generate_json(DbgSetupCtx* ctx, JsonDoc* doc) {
  const JsonVal root = json_add_object(doc);
  json_add_field_lit(doc, root, "version", json_add_string_lit(doc, "0.2.0"));

  const JsonVal configs = json_add_array(doc);
  json_add_field_lit(doc, root, "configurations", configs);
  for (usize i = 0; i != ctx->targetCount; ++i) {
    json_add_elem(doc, configs, dbgsetup_vscode_gen_launch_entry(ctx, doc, ctx->targets[i]));
    json_add_elem(doc, configs, dbgsetup_vscode_gen_attach_entry(ctx, doc, ctx->targets[i]));
  }
  return root;
}

static bool dbgsetup_vscode_generate_launch_file(DbgSetupCtx* ctx) {
  JsonDoc* jsonDoc = json_create(g_allocHeap, 1024);

  const String path = path_build_scratch(ctx->workspace, string_lit(".vscode/launch.json"));
  const bool res = dbgsetup_write_json(path, jsonDoc, dbgsetup_vscode_generate_json(ctx, jsonDoc));

  if (res) {
    log_i("Generated VSCode launch config", log_param("path", fmt_path(path)));
  }

  json_destroy(jsonDoc);
  return res;
}

static CliId g_optDbg, g_optWorkspace, g_optTargets, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Utility to generate debugger configuration files."));

  g_optDbg = cli_register_flag(app, 'd', string_lit("debugger"), CliOptionFlags_Value);
  cli_register_desc_choice_array(
      app, g_optDbg, string_lit("What debugger to use."), g_dbgStrs, DbgSetupDbg_Default);
  cli_register_validator(app, g_optDbg, dbgsetup_validate_dbg);

  g_optWorkspace = cli_register_flag(app, 'w', string_lit("workspace"), CliOptionFlags_Required);
  cli_register_desc(app, g_optWorkspace, string_lit("Project workspace."));

  g_optTargets =
      cli_register_flag(app, 't', string_lit("targets"), CliOptionFlags_RequiredMultiValue);
  cli_register_desc(app, g_optTargets, string_lit("List of debuggable executables."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optDbg, g_optWorkspace, g_optTargets);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, ~LogMask_Debug));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  DbgSetupCtx ctx = {
      .dbg = (DbgSetupDbg)cli_read_choice_array(invoc, g_optDbg, g_dbgStrs, DbgSetupDbg_Default),
      .workspace   = cli_read_string(invoc, g_optWorkspace, string_empty),
      .targets     = cli_parse_values(invoc, g_optTargets).values,
      .targetCount = cli_parse_values(invoc, g_optTargets).count,
  };

  // Sort targets alphabetically.
  // TODO: Its very questionable to modify the collection owned by the cli library.
  sort_quicksort_t(ctx.targets, ctx.targets + ctx.targetCount, String, compare_string);

  log_i(
      "Generating debugger setup",
      log_param("workspace", fmt_path(ctx.workspace)),
      log_param("debugger", fmt_text(g_dbgStrs[ctx.dbg])),
      log_param("targets", fmt_int(ctx.targetCount)));

  return dbgsetup_vscode_generate_launch_file(&ctx) ? 0 : 1;
}
