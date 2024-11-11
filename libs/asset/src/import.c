#include "asset_manager.h"
#include "asset_register.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_path.h"
#include "core_stringtable.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "script_args.h"
#include "script_binder.h"
#include "script_sig.h"

#include "format_internal.h"
#include "import_internal.h"

typedef enum {
  AssetImportType_Mesh,
  AssetImportType_Texture,

  AssetImportType_Count,
  AssetImportType_Sentinel = sentinel_u32
} AssetImportType;

typedef struct {
  bool                 reloading;
  EcsEntityId          asset;
  String               assetId;
  const ScriptProgram* program;
} AssetImportScript;

typedef struct {
  u32      importHash;
  bool     ready;
  DynArray scripts; // AssetImportScript[]
} AssetImportHandler;

static const String g_assetImportScriptPaths[AssetImportType_Count] = {
    [AssetImportType_Mesh]    = string_static("scripts/import/mesh/*.script"),
    [AssetImportType_Texture] = string_static("scripts/import/texture/*.script"),
};

ecs_comp_define(AssetImportEnvComp) { AssetImportHandler handlers[AssetImportType_Count]; };

static void ecs_destruct_import_env_comp(void* data) {
  AssetImportEnvComp* comp = data;
  for (AssetImportType type = 0; type != AssetImportType_Count; ++type) {
    dynarray_destroy(&comp->handlers[type].scripts);
  }
}

static AssetImportType import_type_for_format(const AssetFormat format) {
  switch (format) {
  case AssetFormat_MeshGltf:
    return AssetImportType_Mesh;
  case AssetFormat_TexHeight16:
  case AssetFormat_TexHeight32:
  case AssetFormat_TexPng:
  case AssetFormat_TexPpm:
  case AssetFormat_TexTga:
    return AssetImportType_Texture;

  default:
    return AssetImportType_Sentinel;
  }
}

MAYBE_UNUSED static AssetImportType import_type_for_domain(const AssetScriptDomain domain) {
  switch (domain) {
  case AssetScriptDomain_ImportMesh:
    return AssetImportType_Mesh;
  case AssetScriptDomain_ImportTexture:
    return AssetImportType_Texture;
  default:
    return AssetImportType_Sentinel;
  }
}

static AssetImportEnvComp* import_env_init(EcsWorld* world, AssetManagerComp* manager) {
  AssetImportEnvComp* res = ecs_world_add_t(world, ecs_world_global(world), AssetImportEnvComp);

  EcsEntityId assets[asset_query_max_results];
  u32         assetCount;
  for (AssetImportType type = 0; type != AssetImportType_Count; ++type) {
    assetCount = asset_query(world, manager, g_assetImportScriptPaths[type], assets);

    AssetImportHandler* handler = &res->handlers[type];
    handler->scripts            = dynarray_create_t(g_allocHeap, AssetImportScript, assetCount);
    for (u32 i = 0; i != assetCount; ++i) {
      asset_acquire(world, assets[i]);
      *dynarray_push_t(&handler->scripts, AssetImportScript) = (AssetImportScript){
          .asset = assets[i],
      };
    }
  }
  return res;
}

ecs_view_define(LoadingAssetsView) {
  ecs_access_with(AssetComp);
  ecs_access_with(AssetDirtyComp);
  ecs_access_without(AssetLoadedComp);
  ecs_access_without(AssetFailedComp);
}

ecs_view_define(AssetReloadView) { ecs_access_read(AssetComp); }

ecs_view_define(InitGlobalView) {
  ecs_access_maybe_write(AssetImportEnvComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(InitScriptView) {
  ecs_access_with(AssetLoadedComp);
  ecs_access_without(AssetFailedComp);
  ecs_access_without(AssetChangedComp);
  ecs_access_read(AssetScriptComp);
  ecs_access_read(AssetComp);
}

static void import_reload_all(EcsWorld* world, const AssetImportType type) {
  EcsView* reloadView = ecs_world_view_t(world, AssetReloadView);
  for (EcsIterator* itr = ecs_view_itr(reloadView); ecs_view_walk(itr);) {
    const String      assetId = asset_id(ecs_view_read_t(itr, AssetComp));
    const AssetFormat format  = asset_format_from_ext(path_extension(assetId));
    if (import_type_for_format(format) == type) {
      asset_reload_request(world, ecs_view_entity(itr));
    }
  }
}

static void import_init_handler(
    EcsWorld*             world,
    const AssetImportType type,
    AssetImportHandler*   handler,
    EcsIterator*          scriptItr) {
  /**
   * Update the import scripts.
   * NOTE: Block unloading import scripts when we are currently loading an asset to make sure the
   * importers stay consistent throughout the whole asset load process.
   * NOTE: Refresh the program pointers at the beginning of each frame as the ECS can move component
   * data around during flushes.
   */
  const bool canUnload = !handler->importHash || !ecs_utils_any(world, LoadingAssetsView);

  u32  importHash = 0;
  bool ready      = true;
  dynarray_for_t(&handler->scripts, AssetImportScript, script) {
    const bool isLoaded   = ecs_world_has_t(world, script->asset, AssetLoadedComp);
    const bool isFailed   = ecs_world_has_t(world, script->asset, AssetFailedComp);
    const bool hasChanged = ecs_world_has_t(world, script->asset, AssetChangedComp);

    if (canUnload && hasChanged && !script->reloading && (isLoaded || isFailed)) {
      log_i("Reloading import script", log_param("reason", fmt_text_lit("Asset changed")));

      asset_release(world, script->asset);
      script->reloading = true;
    }

    if (!isFailed && !script->reloading && ecs_view_maybe_jump(scriptItr, script->asset)) {
      const AssetComp*       assetComp  = ecs_view_read_t(scriptItr, AssetComp);
      const AssetScriptComp* scriptComp = ecs_view_read_t(scriptItr, AssetScriptComp);
      diag_assert(type == import_type_for_domain(scriptComp->domain));

      importHash      = bits_hash_32_combine(importHash, scriptComp->hash);
      script->program = &scriptComp->prog;
      script->assetId = asset_id(assetComp);
    } else {
      script->program = null;
      script->assetId = string_empty;
      ready           = false;
    }

    if (script->reloading && !isLoaded) {
      asset_acquire(world, script->asset);
      script->reloading = false;
    }
  }

  if (ready && importHash != handler->importHash) {
    import_reload_all(world, type);
    handler->importHash = importHash;
  }
  handler->ready = ready;
}

ecs_system_define(AssetImportInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, InitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized.
  }
  AssetManagerComp*   manager   = ecs_view_write_t(globalItr, AssetManagerComp);
  AssetImportEnvComp* importEnv = ecs_view_write_t(globalItr, AssetImportEnvComp);
  if (UNLIKELY(!importEnv)) {
    importEnv = import_env_init(world, manager);
  }

  EcsView*     scriptView = ecs_world_view_t(world, InitScriptView);
  EcsIterator* scriptItr  = ecs_view_itr(scriptView);

  for (AssetImportType type = 0; type != AssetImportType_Count; ++type) {
    import_init_handler(world, type, &importEnv->handlers[type], scriptItr);
  }
}

ecs_view_define(DeinitGlobalView) { ecs_access_write(AssetImportEnvComp); }

static void import_deinit_handler(AssetImportHandler* handler) {
  handler->ready = false;
  // Clear program pointers; will be refreshed next frame.
  dynarray_for_t(&handler->scripts, AssetImportScript, script) { script->program = null; }
}

ecs_system_define(AssetImportDeinitSys) {
  EcsView*     globalView = ecs_world_view_t(world, DeinitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (globalItr) {
    AssetImportEnvComp* importEnv = ecs_view_write_t(globalItr, AssetImportEnvComp);

    for (AssetImportType type = 0; type != AssetImportType_Count; ++type) {
      import_deinit_handler(&importEnv->handlers[type]);
    }
  }
}

ecs_module_init(asset_import_module) {
  ecs_register_comp(AssetImportEnvComp, .destructor = ecs_destruct_import_env_comp);

  ecs_register_view(LoadingAssetsView);
  ecs_register_view(AssetReloadView);
  ecs_register_view(InitGlobalView);
  ecs_register_view(InitScriptView);
  ecs_register_view(DeinitGlobalView);

  ecs_register_system(
      AssetImportInitSys,
      ecs_view_id(LoadingAssetsView),
      ecs_view_id(AssetReloadView),
      ecs_view_id(InitGlobalView),
      ecs_view_id(InitScriptView));
  ecs_order(AssetImportInitSys, AssetOrder_Init);

  ecs_register_system(AssetImportDeinitSys, ecs_view_id(DeinitGlobalView));
  ecs_order(AssetImportDeinitSys, AssetOrder_Deinit);
}

bool asset_import_ready(const AssetImportEnvComp* env, const String assetId) {
  /**
   * Check if we are ready to import an asset with the given id.
   *
   * NOTE: Unfortunately its not enough to check if the handler for the asset is ready as the asset
   * can depend on assets of other formats. To make this more optimal we could define for each
   * asset-format which other formats they can depend on (as in practice its quite limited).
   *
   * NOTE: We cannot have an importer for scripts or for asset-formats that depend on scripts,
   * reason is we always need to be able to freely load the importer scripts.
   */
  const AssetFormat format = asset_format_from_ext(path_extension(assetId));
  if (format == AssetFormat_Script) {
    return true;
  }
  for (AssetImportType type = 0; type != AssetImportType_Count; ++type) {
    if (!env->handlers[type].ready) {
      return false;
    }
  }
  return true;
}

u32 asset_import_hash(const AssetImportEnvComp* env, const String assetId) {
  const AssetFormat     format = asset_format_from_ext(path_extension(assetId));
  const AssetImportType type   = import_type_for_format(format);
  if (type == AssetImportType_Sentinel) {
    return 0; // No import-type defined for this format.
  }
  diag_assert_msg(env->handlers[type].ready, "Unable to compute import-hash: Not ready");
  return env->handlers[type].importHash;
}

static void import_log(AssetImportContext* ctx, ScriptBinderCall* call, const LogLevel logLevel) {
  DynString buffer = dynstring_create_over(alloc_alloc(g_allocScratch, usize_kibibyte, 1));
  for (u16 i = 0; i != call->argCount; ++i) {
    if (i) {
      dynstring_append_char(&buffer, ' ');
    }
    script_val_write(call->args[i], &buffer);
  }

  const ScriptRangeLineCol scriptRange    = script_prog_location(ctx->prog, call->callId);
  const String             scriptRangeStr = fmt_write_scratch(
      "{}:{}-{}:{}",
      fmt_int(scriptRange.start.line + 1),
      fmt_int(scriptRange.start.column + 1),
      fmt_int(scriptRange.end.line + 1),
      fmt_int(scriptRange.end.column + 1));

  log(g_logger,
      logLevel,
      "import: {}",
      log_param("text", fmt_text(dynstring_view(&buffer))),
      log_param("asset", fmt_text(ctx->assetId)),
      log_param("script", fmt_text(ctx->progId)),
      log_param("script-range", fmt_text(scriptRangeStr)));
}

static ScriptVal import_eval_asset_id(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  const StringHash assetIdHash = stringtable_add(g_stringtable, ctx->assetId);
  return script_str(assetIdHash);
}

static ScriptVal import_eval_asset_id_match(AssetImportContext* ctx, ScriptBinderCall* call) {
  const StringHash patternHash = script_arg_str(call, 0);
  if (UNLIKELY(script_call_panicked(call))) {
    return script_bool(false);
  }
  const String patternStr = stringtable_lookup(g_stringtable, patternHash);
  return script_bool(string_match_glob(ctx->assetId, patternStr, StringMatchFlags_IgnoreCase));
}

static ScriptVal import_eval_asset_format(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  const AssetFormat format        = asset_format_from_ext(path_extension(ctx->assetId));
  const StringHash  formatStrHash = stringtable_add(g_stringtable, asset_format_str(format));
  return script_str(formatStrHash);
}

static ScriptVal import_eval_log(AssetImportContext* ctx, ScriptBinderCall* call) {
  import_log(ctx, call, LogLevel_Info);
  return script_null();
}

static ScriptVal import_eval_warn(AssetImportContext* ctx, ScriptBinderCall* call) {
  import_log(ctx, call, LogLevel_Warn);
  return script_null();
}

static ScriptVal import_eval_fail(AssetImportContext* ctx, ScriptBinderCall* call) {
  import_log(ctx, call, LogLevel_Error);
  ctx->failed = true;
  return script_null();
}

static ScriptVal import_eval_fail_if(AssetImportContext* ctx, ScriptBinderCall* call) {
  const ScriptVal cond = script_arg_any(call, 0);
  if (!script_call_panicked(call) && script_truthy(cond)) {
    script_arg_shift(call);
    import_log(ctx, call, LogLevel_Error);
    ctx->failed = true;
  }
  return script_null();
}

static ScriptVal import_eval_failed(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  return script_bool(ctx->failed);
}

void asset_import_register(ScriptBinder* binder) {
  // clang-format off
  static const String g_globPatternDoc = string_static("Supported pattern syntax:\n- '?' matches any single character.\n- '*' matches any number of any characters including none.\n- '!' inverts the entire match (not per segment and cannot be disabled after enabling).");
  {
    const String       name   = string_lit("asset_id");
    const String       doc    = string_lit("Lookup the identifier of the importing asset.");
    const ScriptMask   ret    = script_mask_str;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_asset_id);
  }
  {
    const String       name   = string_lit("asset_id_match");
    const String       doc    = fmt_write_scratch("Match the identifier of the importing asset against the given glob pattern.\n\n{}", fmt_text(g_globPatternDoc));
    const ScriptMask   ret    = script_mask_bool;
    const ScriptSigArg args[] = {
        {string_lit("pattern"), script_mask_str},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_asset_id_match);
  }
  {
    const String       name   = string_lit("asset_format");
    const String       doc    = string_lit("Lookup the format of the importing asset.");
    const ScriptMask   ret    = script_mask_str;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_asset_format);
  }
  {
    const String       name   = string_lit("log");
    const String       doc    = string_lit("Log the given values.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("values"), script_mask_any, ScriptSigArgFlags_Multi},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_log);
  }
  {
    const String       name   = string_lit("warn");
    const String       doc    = string_lit("Log a warning with the given values.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("values"), script_mask_any, ScriptSigArgFlags_Multi},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_warn);
  }
  {
    const String       name   = string_lit("fail");
    const String       doc    = string_lit("Fail the import and log an error with the given values.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("values"), script_mask_any, ScriptSigArgFlags_Multi},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_fail);
  }
  {
    const String       name   = string_lit("fail_if");
    const String       doc    = string_lit("Fail the import if the given value is truthy.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("condition"), script_mask_bool},
        {string_lit("message"), script_mask_str},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_fail_if);
  }
  {
    const String       name   = string_lit("failed");
    const String       doc    = string_lit("Check if the import has failed.");
    const ScriptMask   ret    = script_mask_bool;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_failed);
  }
  // clang-format on
}

void asset_import_bind(
    ScriptBinder*               binder,
    const String                name,
    const String                doc,
    const ScriptMask            retMask,
    const ScriptSigArg*         args,
    const u8                    argCount,
    const AssetImportBinderFunc func) {
  const ScriptSig* sig = script_sig_create(g_allocScratch, retMask, args, argCount);
  // NOTE: Func pointer cast is needed to type-erase the context type.
  script_binder_declare(binder, name, doc, sig, (ScriptBinderFunc)func);
}

bool asset_import_eval(
    const AssetImportEnvComp* env, const ScriptBinder* binder, const String assetId, void* data) {
  const AssetFormat     format = asset_format_from_ext(path_extension(assetId));
  const AssetImportType type   = import_type_for_format(format);
  diag_assert(type != AssetImportType_Sentinel);

  const AssetImportHandler* handler = &env->handlers[type];
  diag_assert(handler->ready);

  AssetImportContext ctx = {
      .assetId = assetId,
      .data    = data,
  };

  dynarray_for_t(&handler->scripts, AssetImportScript, script) {
    ctx.prog   = script->program;
    ctx.progId = script->assetId;

    const ScriptProgResult evalRes = script_prog_eval(script->program, null, binder, &ctx);
    if (UNLIKELY(evalRes.panic.kind)) {
      const String msg            = script_panic_scratch(&evalRes.panic, ScriptPanicOutput_Default);
      const String scriptRangeStr = fmt_write_scratch(
          "{}:{}-{}:{}",
          fmt_int(evalRes.panic.range.start.line + 1),
          fmt_int(evalRes.panic.range.start.column + 1),
          fmt_int(evalRes.panic.range.end.line + 1),
          fmt_int(evalRes.panic.range.end.column + 1));

      log_e(
          "Import script panic",
          log_param("panic", fmt_text(msg)),
          log_param("script", fmt_text(script->assetId)),
          log_param("script-range", fmt_text(scriptRangeStr)),
          log_param("asset", fmt_text(assetId)));

      ctx.failed = true;
    }
  }

  return !ctx.failed;
}
