#include "asset_manager.h"
#include "asset_register.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_format.h"
#include "ecs_world.h"
#include "script_binder.h"
#include "script_sig.h"

#include "import_internal.h"

ScriptBinder* g_assetScriptImportBinder;

ecs_comp_define(AssetImportEnvComp) { u32 dummy; };

static void ecs_destruct_import_env_comp(void* data) {
  AssetImportEnvComp* comp = data;
  (void)comp;
}

typedef struct {
  u32 dummy;
} AssetImportContext;

static ScriptVal eval_dummy(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)ctx;
  (void)call;
  return script_null();
}

ecs_view_define(InitGlobalView) {
  ecs_access_maybe_write(AssetImportEnvComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(AssetImportInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, InitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized.
  }
  AssetImportEnvComp* importEnv = ecs_view_write_t(globalItr, AssetImportEnvComp);
  if (UNLIKELY(!importEnv)) {
    importEnv = ecs_world_add_t(world, ecs_world_global(world), AssetImportEnvComp);
  }
}

ecs_module_init(asset_import_module) {
  ecs_register_comp(AssetImportEnvComp, .destructor = ecs_destruct_import_env_comp);

  ecs_register_view(InitGlobalView);

  ecs_register_system(AssetImportInitSys, ecs_view_id(InitGlobalView));

  ecs_order(AssetImportInitSys, AssetOrder_Init);
}

typedef ScriptVal (*ImportBinderFunc)(AssetImportContext*, ScriptBinderCall*);

static void bind_eval(
    ScriptBinder*          binder,
    const String           name,
    const String           doc,
    const ScriptMask       retMask,
    const ScriptSigArg     args[],
    const u8               argCount,
    const ImportBinderFunc func) {
  const ScriptSig* sig = script_sig_create(g_allocScratch, retMask, args, argCount);
  // NOTE: Func pointer cast is needed to type-erase the context type.
  script_binder_declare(binder, name, doc, sig, (ScriptBinderFunc)func);
}

void asset_data_init_import(void) {
  const ScriptBinderFlags flags = ScriptBinderFlags_DisallowMemoryAccess;
  ScriptBinder* binder          = script_binder_create(g_allocPersist, string_lit("import"), flags);
  script_binder_filter_set(binder, string_lit("import/*.script"));

  // clang-format off
  {
    const String       name   = string_lit("dummy");
    const String       doc    = fmt_write_scratch("Set a dummy import config");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("val"), script_mask_null},
    };
    bind_eval(binder, name, doc, ret, args, array_elems(args), eval_dummy);
  }
  // clang-format on

  script_binder_finalize(binder);
  g_assetScriptImportBinder = binder;
}
