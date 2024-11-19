#include "core_alloc.h"
#include "core_array.h"
#include "core_format.h"
#include "script_args.h"
#include "script_binder.h"
#include "script_sig.h"

#include "import_mesh_internal.h"

ScriptBinder* g_assetScriptImportMeshBinder;

static ScriptVal import_eval_vertex_scale(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data = ctx->data;
  if (call->argCount < 1) {
    return script_num(data->vertexScale);
  }
  const f64 scale = script_arg_num_range(call, 0, 1e-3, 1e+6);
  if (!script_call_panicked(call)) {
    data->vertexScale = (f32)scale;
  }
  return script_null();
}

static ScriptVal import_eval_joint_count(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  AssetImportMesh* data = ctx->data;
  return script_num(data->jointCount);
}

void asset_data_init_import_mesh(void) {
  const ScriptBinderFlags flags = ScriptBinderFlags_DisallowMemoryAccess;
  ScriptBinder* binder = script_binder_create(g_allocPersist, string_lit("import-mesh"), flags);
  script_binder_filter_set(binder, string_lit("import/mesh/*.script"));

  // clang-format off
  {
    const String       name   = string_lit("vertex_scale");
    const String       doc    = fmt_write_scratch("Set the vertex import scale.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("scale"), script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_vertex_scale);
  }
  {
    const String       name   = string_lit("joint_count");
    const String       doc    = fmt_write_scratch("Query the amount of joints in the mesh.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_joint_count);
  }
  // clang-format on

  asset_import_register(binder);

  script_binder_finalize(binder);
  g_assetScriptImportMeshBinder = binder;
}

bool asset_import_mesh(const AssetImportEnvComp* env, const String id, AssetImportMesh* data) {
  return asset_import_eval(env, g_assetScriptImportMeshBinder, id, data);
}
