#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_format.h"
#include "core_stringtable.h"
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

static ScriptVal import_eval_joint_find(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data      = ctx->data;
  const StringHash jointName = script_arg_str(call, 0);
  if (!script_call_panicked(call)) {
    for (u32 jointIndex = 0; jointIndex != data->jointCount; ++jointIndex) {
      if (data->joints[jointIndex].nameHash == jointName) {
        return script_num(jointIndex);
      }
    }
  }
  return script_null();
}

static ScriptVal import_eval_joint_name(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data  = ctx->data;
  const u32        index = (u32)script_arg_num_range(call, 0, 0, data->jointCount - 1);
  if (script_call_panicked(call)) {
    return script_null();
  }
  diag_assert(index < data->jointCount);
  if (call->argCount < 2) {
    return script_str(data->joints[index].nameHash);
  }
  const StringHash newName = script_arg_str(call, 1);
  if (!script_call_panicked(call)) {
    data->joints[index].nameHash = newName;
  }
  return script_null();
}

static ScriptVal import_eval_joint_name_trim(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data       = ctx->data;
  const u32        index      = (u32)script_arg_num_range(call, 0, 0, data->jointCount - 1);
  const StringHash prefixHash = script_arg_str(call, 1);
  const StringHash suffixHash = script_arg_opt_str(call, 2, 0);
  if (script_call_panicked(call)) {
    return script_null();
  }
  if (!data->joints[index].nameHash) {
    return script_str(string_hash_lit(""));
  }
  String name = stringtable_lookup(g_stringtable, data->joints[index].nameHash);

  const String prefix = stringtable_lookup(g_stringtable, prefixHash);
  if (string_starts_with(name, prefix)) {
    name = string_slice(name, prefix.size, name.size - prefix.size);
  }

  const String suffix = suffixHash ? stringtable_lookup(g_stringtable, suffixHash) : string_empty;
  if (string_ends_with(name, suffix)) {
    name = string_slice(name, 0, name.size - suffix.size);
  }

  data->joints[index].nameHash = stringtable_add(g_stringtable, name);
  return script_str(data->joints[index].nameHash);
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
  {
    const String       name   = string_lit("joint_find");
    const String       doc    = fmt_write_scratch("Find a joint with the given name, returns the index of the joint or null if none was found.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("jointName"), script_mask_str},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_joint_find);
  }
  {
    const String       name   = string_lit("joint_name");
    const String       doc    = fmt_write_scratch("Query or change the name of the joint at the given index.");
    const ScriptMask   ret    = script_mask_str | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("newName"), script_mask_str | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_joint_name);
  }
  {
    const String       name   = string_lit("joint_name_trim");
    const String       doc    = fmt_write_scratch("Remove a prefix (and optionally suffix) from the joint name at the given index. Returns the new name.");
    const ScriptMask   ret    = script_mask_str;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("prefix"), script_mask_str},
        {string_lit("suffix"), script_mask_str | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_joint_name_trim);
  }
  // clang-format on

  asset_import_register(binder);

  script_binder_finalize(binder);
  g_assetScriptImportMeshBinder = binder;
}

bool asset_import_mesh(const AssetImportEnvComp* env, const String id, AssetImportMesh* data) {
  return asset_import_eval(env, g_assetScriptImportMeshBinder, id, data);
}
