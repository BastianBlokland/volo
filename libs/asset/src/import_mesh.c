#include "core_alloc.h"
#include "core_array.h"
#include "core_format.h"
#include "script_binder.h"
#include "script_sig.h"

#include "import_mesh_internal.h"

ScriptBinder* g_assetScriptImportMeshBinder;

static ScriptVal eval_dummy(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)ctx;
  (void)call;
  return script_null();
}

void asset_data_init_import_mesh(void) {
  const ScriptBinderFlags flags = ScriptBinderFlags_DisallowMemoryAccess;
  ScriptBinder* binder = script_binder_create(g_allocPersist, string_lit("import-mesh"), flags);
  script_binder_filter_set(binder, string_lit("import/mesh/*.script"));

  // clang-format off
  {
    const String       name   = string_lit("dummy");
    const String       doc    = fmt_write_scratch("Set a dummy import config");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("val"), script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), eval_dummy);
  }
  // clang-format on

  asset_import_register(binder);

  script_binder_finalize(binder);
  g_assetScriptImportMeshBinder = binder;
}

bool asset_import_mesh(const AssetImportEnvComp* env, const String id, AssetImportMesh* out) {
  *out = (AssetImportMesh){
      .scale = 1.0f,
  };
  return asset_import_eval(env, g_assetScriptImportMeshBinder, id, out);
}