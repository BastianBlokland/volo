#include "cli.h"
#include "core.h"
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

typedef struct {
  CliApp* cliApp;
  CliId   dbgFlag, workspaceFlag, targetsFlag, helpFlag;
} DbgSetupApp;

typedef enum {
  DbgSetupDbg_Lldb,
  DbgSetupDbg_Cppvsdbg,
} DbgSetupDbg;

typedef struct {
  DbgSetupDbg dbg;
  String      workspace;
  String*     targets;
  usize       targetCount;
} DbgSetupCtx;

static const String g_dbg_strs[] = {
    string_static("lldb"),
    string_static("cppvsdbg"),
};

static bool dbgsetup_validate_dbg(const String input) {
  array_for_t(g_dbg_strs, String, cfg, {
    if (string_eq(*cfg, input)) {
      return true;
    }
  });
  return false;
}

static bool dbgsetup_write_json(String path, const JsonDoc* jsonDoc, const JsonVal jsonVal) {
  DynString dynString = dynstring_create(g_alloc_heap, 64 * usize_kibibyte);
  json_write(&dynString, jsonDoc, jsonVal, &json_write_opts());

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

static JsonVal dbgsetup_vscode_gen_launch_entry(DbgSetupCtx* ctx, JsonDoc* doc, String target) {
  const JsonVal obj = json_add_object(doc);
  json_add_field_str(
      doc,
      obj,
      string_lit("name"),
      json_add_string(doc, fmt_write_scratch("{} (Launch)", fmt_text(path_stem(target)))));
  json_add_field_str(doc, obj, string_lit("type"), json_add_string(doc, g_dbg_strs[ctx->dbg]));
  json_add_field_str(doc, obj, string_lit("request"), json_add_string_lit(doc, "launch"));
  json_add_field_str(doc, obj, string_lit("program"), json_add_string(doc, target));
  json_add_field_str(doc, obj, string_lit("cwd"), json_add_string(doc, ctx->workspace));
  json_add_field_str(doc, obj, string_lit("args"), json_add_array(doc));
  json_add_field_str(doc, obj, string_lit("terminal"), json_add_string_lit(doc, "integrated"));
  json_add_field_str(doc, obj, string_lit("stopOnEntry"), json_add_bool(doc, false));
  return obj;
}

static JsonVal dbgsetup_vscode_gen_attach_entry(DbgSetupCtx* ctx, JsonDoc* doc, String target) {
  const JsonVal obj = json_add_object(doc);
  json_add_field_str(
      doc,
      obj,
      string_lit("name"),
      json_add_string(doc, fmt_write_scratch("{} (Attach)", fmt_text(path_stem(target)))));
  json_add_field_str(doc, obj, string_lit("type"), json_add_string(doc, g_dbg_strs[ctx->dbg]));
  json_add_field_str(doc, obj, string_lit("request"), json_add_string_lit(doc, "attach"));
  json_add_field_str(doc, obj, string_lit("program"), json_add_string(doc, target));
  json_add_field_str(
      doc, obj, string_lit("pid"), json_add_string_lit(doc, "${command:pickMyProcess}"));
  return obj;
}

static JsonVal dbgsetup_vscode_generate_json(DbgSetupCtx* ctx, JsonDoc* doc) {
  const JsonVal root = json_add_object(doc);
  json_add_field_str(doc, root, string_lit("version"), json_add_string_lit(doc, "0.2.0"));

  const JsonVal configs = json_add_array(doc);
  json_add_field_str(doc, root, string_lit("configurations"), configs);
  for (usize i = 0; i != ctx->targetCount; ++i) {
    json_add_elem(doc, configs, dbgsetup_vscode_gen_launch_entry(ctx, doc, ctx->targets[i]));
    json_add_elem(doc, configs, dbgsetup_vscode_gen_attach_entry(ctx, doc, ctx->targets[i]));
  }
  return root;
}

static bool dbgsetup_vscode_generate_launch_file(DbgSetupCtx* ctx) {
  JsonDoc* jsonDoc = json_create(g_alloc_heap, 1024);

  const String path = path_build_scratch(ctx->workspace, string_lit(".vscode/launch.json"));
  const bool res = dbgsetup_write_json(path, jsonDoc, dbgsetup_vscode_generate_json(ctx, jsonDoc));

  if (res) {
    log_i("Generated VSCode launch config", log_param("path", fmt_path(path)));
  }

  json_destroy(jsonDoc);
  return res;
}

static DbgSetupApp dbgsetup_app_create() {
  const String desc = string_lit("Utility to generate debugger configuration files.");
  CliApp*      app  = cli_app_create(g_alloc_heap, desc);

  const CliId dbgFlag = cli_register_flag(app, 'd', string_lit("debugger"), CliOptionFlags_Value);
  cli_register_desc_choice_array(
      app, dbgFlag, string_lit("What debugger to use."), g_dbg_strs, DbgSetupDbg_Lldb);
  cli_register_validator(app, dbgFlag, dbgsetup_validate_dbg);

  const CliId workspaceFlag =
      cli_register_flag(app, 'w', string_lit("workspace"), CliOptionFlags_Required);
  cli_register_desc(app, workspaceFlag, string_lit("Project workspace."));

  const CliId targetsFlag =
      cli_register_flag(app, 't', string_lit("targets"), CliOptionFlags_RequiredMultiValue);
  cli_register_desc(app, targetsFlag, string_lit("List of debuggable executables."));

  const CliId helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, helpFlag, string_lit("Display this help page."));
  cli_register_exclusions(app, helpFlag, dbgFlag, workspaceFlag, targetsFlag);

  return (DbgSetupApp){
      .cliApp        = app,
      .dbgFlag       = dbgFlag,
      .workspaceFlag = workspaceFlag,
      .targetsFlag   = targetsFlag,
      .helpFlag      = helpFlag,
  };
}

static void dbgsetup_app_destroy(DbgSetupApp* app) { cli_app_destroy(app->cliApp); }

static int dbgsetup_app_run(DbgSetupApp* app, const int argc, const char** argv) {
  int result = 0;

  CliInvocation* invoc = cli_parse(app->cliApp, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    result = 2;
    goto exit;
  }

  if (cli_parse_provided(invoc, app->helpFlag)) {
    cli_help_write_file(app->cliApp, g_file_stdout);
    goto exit;
  }

  DbgSetupCtx ctx = {
      .dbg         = cli_read_choice_array(invoc, app->dbgFlag, g_dbg_strs, DbgSetupDbg_Lldb),
      .workspace   = cli_read_string(invoc, app->workspaceFlag, string_empty),
      .targets     = cli_parse_values(invoc, app->targetsFlag).head,
      .targetCount = cli_parse_values(invoc, app->targetsFlag).count,
  };

  // Sort targets alphabetically.
  sort_quicksort_t(ctx.targets, ctx.targets + ctx.targetCount, String, compare_string);

  log_i(
      "Generating debugger setup",
      log_param("workspace", fmt_path(ctx.workspace)),
      log_param("debugger", fmt_text(g_dbg_strs[ctx.dbg])),
      log_param("targets", fmt_int(ctx.targetCount)));

  if (!dbgsetup_vscode_generate_launch_file(&ctx)) {
    result = 1;
    goto exit;
  }

exit:
  cli_parse_destroy(invoc);
  return result;
}

int main(const int argc, const char** argv) {
  core_init();
  log_init();

  log_add_sink(g_logger, log_sink_pretty_default(g_alloc_heap, ~LogMask_Debug));
  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  DbgSetupApp app = dbgsetup_app_create();

  const int exitCode = dbgsetup_app_run(&app, argc, argv);

  dbgsetup_app_destroy(&app);

  log_teardown();
  core_teardown();
  return exitCode;
}
