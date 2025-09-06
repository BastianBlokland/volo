#include "app/cli.h"
#include "cli/app.h"
#include "cli/parse.h"
#include "cli/read.h"
#include "core/alloc.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/path.h"
#include "core/sort.h"
#include "json/doc.h"
#include "json/write.h"
#include "log/logger.h"
#include "log/sink_json.h"
#include "log/sink_pretty.h"

/**
 * DebugGen - Utility to generate debugger configuration files for a set of executables.
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
 *       "args": []
 *     }
 *   ]
 * }
 * ```
 */

typedef enum {
  DbgGenDbg_Lldb,
  DbgGenDbg_Cppvsdbg,

  DbgGenDbg_Count,
  DbgGenDbg_Default = DbgGenDbg_Lldb,
} DbgGenDbg;

static const String g_dbgStrs[] = {
    string_static("lldb"),
    string_static("cppvsdbg"),
};
ASSERT(array_elems(g_dbgStrs) == DbgGenDbg_Count, "Incorrect number of dbg strings");

static bool dbggen_validate_dbg(const String input) {
  array_for_t(g_dbgStrs, String, cfg) {
    if (string_eq(*cfg, input)) {
      return true;
    }
  }
  return false;
}

typedef struct {
  DbgGenDbg dbg;
  String    workspace;
  String*   targets;
  usize     targetCount;
} DbgGenCtx;

static bool dbggen_write_json(String path, const JsonDoc* jsonDoc, const JsonVal jsonVal) {
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

static JsonVal dbggen_vscode_gen_launch_entry(DbgGenCtx* ctx, JsonDoc* doc, String target) {
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
  return obj;
}

static JsonVal dbggen_vscode_gen_attach_entry(DbgGenCtx* ctx, JsonDoc* doc, String target) {
  const JsonVal obj = json_add_object(doc);
  json_add_field_lit(
      doc,
      obj,
      "name",
      json_add_string(doc, fmt_write_scratch("{} (Attach)", fmt_text(path_stem(target)))));
  json_add_field_lit(doc, obj, "type", json_add_string(doc, g_dbgStrs[ctx->dbg]));
  json_add_field_lit(doc, obj, "request", json_add_string_lit(doc, "attach"));
  json_add_field_lit(doc, obj, "program", json_add_string(doc, target));
  return obj;
}

static JsonVal dbggen_vscode_generate_json(DbgGenCtx* ctx, JsonDoc* doc) {
  const JsonVal root = json_add_object(doc);
  json_add_field_lit(doc, root, "version", json_add_string_lit(doc, "0.2.0"));

  const JsonVal configs = json_add_array(doc);
  json_add_field_lit(doc, root, "configurations", configs);
  for (usize i = 0; i != ctx->targetCount; ++i) {
    json_add_elem(doc, configs, dbggen_vscode_gen_launch_entry(ctx, doc, ctx->targets[i]));
    json_add_elem(doc, configs, dbggen_vscode_gen_attach_entry(ctx, doc, ctx->targets[i]));
  }
  return root;
}

static bool dbggen_vscode_generate_launch_file(DbgGenCtx* ctx) {
  JsonDoc* jsonDoc = json_create(g_allocHeap, 1024);

  const String path = path_build_scratch(ctx->workspace, string_lit(".vscode/launch.json"));
  const bool   res  = dbggen_write_json(path, jsonDoc, dbggen_vscode_generate_json(ctx, jsonDoc));

  if (res) {
    log_i("Generated VSCode launch config", log_param("path", fmt_path(path)));
  }

  json_destroy(jsonDoc);
  return res;
}

static CliId g_optDbg, g_optWorkspace, g_optTargets;

AppType app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Utility to generate debugger configuration files."));

  g_optDbg = cli_register_flag(app, 'd', string_lit("debugger"), CliOptionFlags_Value);
  cli_register_desc_choice_array(
      app, g_optDbg, string_lit("What debugger to use."), g_dbgStrs, DbgGenDbg_Default);
  cli_register_validator(app, g_optDbg, dbggen_validate_dbg);

  g_optWorkspace = cli_register_flag(app, 'w', string_lit("workspace"), CliOptionFlags_Required);
  cli_register_desc(app, g_optWorkspace, string_lit("Project workspace."));

  g_optTargets =
      cli_register_flag(app, 't', string_lit("targets"), CliOptionFlags_RequiredMultiValue);
  cli_register_desc(app, g_optTargets, string_lit("List of debuggable executables."));

  return AppType_Console;
}

i32 app_cli_run(MAYBE_UNUSED const CliApp* app, const CliInvocation* invoc) {

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, g_fileStdOut, ~LogMask_Debug));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  const CliParseValues targets = cli_parse_values(invoc, g_optTargets);

  // Sort targets alphabetically.
  String* targetsSorted = null;
  if (targets.count) {
    targetsSorted = alloc_array_t(g_allocHeap, String, targets.count);

    const usize memSize = targets.count * sizeof(String);
    mem_cpy(mem_create(targetsSorted, memSize), mem_create(targets.values, memSize));

    sort_quicksort_t(targetsSorted, targetsSorted + targets.count, String, compare_string);
  }

  DbgGenCtx ctx = {
      .dbg       = (DbgGenDbg)cli_read_choice_array(invoc, g_optDbg, g_dbgStrs, DbgGenDbg_Default),
      .workspace = cli_read_string(invoc, g_optWorkspace, string_empty),
      .targets   = targetsSorted,
      .targetCount = targets.count,
  };

  log_i(
      "Generating debugger setup",
      log_param("workspace", fmt_path(ctx.workspace)),
      log_param("debugger", fmt_text(g_dbgStrs[ctx.dbg])),
      log_param("targets", fmt_int(ctx.targetCount)));

  const i32 res = dbggen_vscode_generate_launch_file(&ctx) ? 0 : 1;
  if (targetsSorted) {
    alloc_free_array_t(g_allocHeap, targetsSorted, targets.count);
  }
  return res;
}
