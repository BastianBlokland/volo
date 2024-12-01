#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_format.h"
#include "core_sort.h"
#include "core_stringtable.h"
#include "log_logger.h"
#include "script_args.h"
#include "script_binder.h"
#include "script_enum.h"
#include "script_panic.h"
#include "script_sig.h"

#include "import_mesh_internal.h"

ScriptBinder* g_assetScriptImportMeshBinder;

static ScriptEnum g_importAnimFlags;

static void import_init_enum_anim_flags(void) {
#define ENUM_PUSH(_ENUM_, _NAME_)                                                                  \
  script_enum_push((_ENUM_), string_lit(#_NAME_), AssetMeshAnimFlags_##_NAME_);

  ENUM_PUSH(&g_importAnimFlags, Active);
  ENUM_PUSH(&g_importAnimFlags, Loop);
  ENUM_PUSH(&g_importAnimFlags, FadeIn);
  ENUM_PUSH(&g_importAnimFlags, FadeOut);
  ENUM_PUSH(&g_importAnimFlags, RandomTime);

#undef ENUM_PUSH
}

static i8 import_compare_anim_layer(const void* a, const void* b) {
  return compare_i32(field_ptr(a, AssetImportAnim, layer), field_ptr(b, AssetImportAnim, layer));
}

static f32 import_mesh_clamp01(const f32 val) {
  if (val <= 0.0f) {
    return 0.0f;
  }
  if (val >= 1.0f) {
    return 1.0f;
  }
  return val;
}

static bool import_mesh_joint_find_duplicate(AssetImportMesh* data, StringHash* outDuplicate) {
  if (!data->jointCount) {
    return false;
  }
  for (u32 i = 0; i != data->jointCount - 1; ++i) {
    for (u32 j = i + 1; j != data->jointCount; ++j) {
      if (data->joints[i].nameHash == data->joints[j].nameHash) {
        return *outDuplicate = data->joints[i].nameHash, true; // Duplicate found.
      }
    }
  }
  return false;
}

static bool import_mesh_anim_find_duplicate(AssetImportMesh* data, StringHash* outDuplicate) {
  if (!data->animCount) {
    return false;
  }
  for (u32 i = 0; i != data->animCount - 1; ++i) {
    for (u32 j = i + 1; j != data->animCount; ++j) {
      if (data->anims[i].nameHash == data->anims[j].nameHash) {
        return *outDuplicate = data->anims[i].nameHash, true; // Duplicate found.
      }
    }
  }
  return false;
}

static ScriptVal import_eval_flat_normals(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data = ctx->data;
  if (call->argCount < 1) {
    return script_bool(data->flatNormals);
  }
  data->flatNormals = script_arg_bool(call, 0);
  return script_null();
}

static ScriptVal import_eval_vertex_translation(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data = ctx->data;
  if (call->argCount < 1) {
    return script_vec3(data->vertexTranslation);
  }
  data->vertexTranslation = script_arg_vec3(call, 0);
  return script_null();
}

static ScriptVal import_eval_vertex_rotation(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data = ctx->data;
  if (call->argCount < 1) {
    return script_quat(data->vertexRotation);
  }
  data->vertexRotation = script_arg_quat(call, 0);
  return script_null();
}

static ScriptVal import_eval_vertex_scale(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data = ctx->data;
  if (call->argCount < 1) {
    return script_vec3(data->vertexScale);
  }
  script_arg_check(call, 0, script_mask_num | script_mask_vec3);
  if (script_type(call->args[0]) == ScriptType_Num) {
    const f32 scale   = (f32)script_arg_num_range(call, 0, 1e-3, 1e+6);
    data->vertexScale = geo_vector(scale, scale, scale);
  } else {
    data->vertexScale = script_arg_vec3(call, 0);
  }
  return script_null();
}

static ScriptVal import_eval_root_translation(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data = ctx->data;
  if (call->argCount < 1) {
    return script_vec3(data->rootTranslation);
  }
  data->rootTranslation = script_arg_vec3(call, 0);
  return script_null();
}

static ScriptVal import_eval_root_rotation(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data = ctx->data;
  if (call->argCount < 1) {
    return script_quat(data->rootRotation);
  }
  data->rootRotation = script_arg_quat(call, 0);
  return script_null();
}

static ScriptVal import_eval_root_scale(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data = ctx->data;
  if (call->argCount < 1) {
    return script_vec3(data->rootScale);
  }
  script_arg_check(call, 0, script_mask_num | script_mask_vec3);
  if (script_type(call->args[0]) == ScriptType_Num) {
    const f32 scale = (f32)script_arg_num_range(call, 0, 1e-3, 1e+6);
    data->rootScale = geo_vector(scale, scale, scale);
  } else {
    data->rootScale = script_arg_vec3(call, 0);
  }
  return script_null();
}

static ScriptVal import_eval_joint_count(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  AssetImportMesh* data = ctx->data;
  return script_num(data->jointCount);
}

static ScriptVal import_eval_joint_parent(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data  = ctx->data;
  const u32        index = (u32)script_arg_num_range(call, 0, 0, data->jointCount - 1);
  diag_assert(index < data->jointCount);
  return script_num(data->joints[index].parentIndex);
}

static ScriptVal import_eval_joint_find(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data      = ctx->data;
  const StringHash jointName = script_arg_str(call, 0);
  for (u32 jointIndex = 0; jointIndex != data->jointCount; ++jointIndex) {
    if (data->joints[jointIndex].nameHash == jointName) {
      return script_num(jointIndex);
    }
  }
  return script_null();
}

static ScriptVal import_eval_joint_name(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data  = ctx->data;
  const u32        index = (u32)script_arg_num_range(call, 0, 0, data->jointCount - 1);
  diag_assert(index < data->jointCount);
  if (call->argCount < 2) {
    return script_str(data->joints[index].nameHash);
  }
  data->joints[index].nameHash = script_arg_str(call, 1);
  return script_null();
}

static ScriptVal import_eval_joint_name_trim(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data       = ctx->data;
  const u32        index      = (u32)script_arg_num_range(call, 0, 0, data->jointCount - 1);
  const StringHash prefixHash = script_arg_str(call, 1);
  const StringHash suffixHash = script_arg_opt_str(call, 2, 0);
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

static ScriptVal import_eval_anim_count(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)call;
  AssetImportMesh* data = ctx->data;
  return script_num(data->animCount);
}

static ScriptVal import_eval_anim_find(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data     = ctx->data;
  const StringHash animName = script_arg_str(call, 0);
  for (u32 animIndex = 0; animIndex != data->animCount; ++animIndex) {
    if (data->anims[animIndex].nameHash == animName) {
      return script_num(animIndex);
    }
  }
  return script_null();
}

static ScriptVal import_eval_anim_layer(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data  = ctx->data;
  const u32        index = (u32)script_arg_num_range(call, 0, 0, data->animCount - 1);
  diag_assert(index < data->animCount);
  if (call->argCount < 2) {
    return script_num(data->anims[index].layer);
  }
  data->anims[index].layer = (i32)script_arg_num(call, 1);
  return script_null();
}

static ScriptVal import_eval_anim_flag(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data  = ctx->data;
  const u32        index = (u32)script_arg_num_range(call, 0, 0, data->animCount - 1);
  diag_assert(index < data->animCount);
  const i32 flag = script_arg_enum(call, 1, &g_importAnimFlags);
  if (call->argCount < 3) {
    return script_bool((data->anims[index].flags & flag) != 0);
  }
  const bool enabled = script_arg_bool(call, 2);
  if (enabled != !!(data->anims[index].flags & flag)) {
    data->anims[index].flags ^= flag;
  }
  return script_null();
}

static ScriptVal import_eval_anim_name(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data  = ctx->data;
  const u32        index = (u32)script_arg_num_range(call, 0, 0, data->animCount - 1);
  diag_assert(index < data->animCount);
  if (call->argCount < 2) {
    return script_str(data->anims[index].nameHash);
  }
  data->anims[index].nameHash = script_arg_str(call, 1);
  return script_null();
}

static ScriptVal import_eval_anim_duration(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data  = ctx->data;
  const u32        index = (u32)script_arg_num_range(call, 0, 0, data->animCount - 1);
  diag_assert(index < data->animCount);
  if (call->argCount < 2) {
    return script_num(data->anims[index].duration);
  }
  data->anims[index].duration = (f32)script_arg_num_range(call, 1, 1e-4, 1e+4);
  return script_null();
}

static ScriptVal import_eval_anim_time(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data  = ctx->data;
  const u32        index = (u32)script_arg_num_range(call, 0, 0, data->animCount - 1);
  diag_assert(index < data->animCount);
  if (call->argCount < 2) {
    return script_num(data->anims[index].time);
  }
  data->anims[index].time = (f32)script_arg_num_range(call, 1, 0.0, 1e+4);
  return script_null();
}

static ScriptVal import_eval_anim_speed(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data  = ctx->data;
  const u32        index = (u32)script_arg_num_range(call, 0, 0, data->animCount - 1);
  diag_assert(index < data->animCount);
  if (call->argCount < 2) {
    return script_num(data->anims[index].speed);
  }
  data->anims[index].speed         = (f32)script_arg_num_range(call, 1, 0.0, 1e3);
  data->anims[index].speedVariance = (f32)script_arg_opt_num_range(call, 2, 0.0, 1e3, 0.0);
  return script_null();
}

static ScriptVal import_eval_anim_weight(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data  = ctx->data;
  const u32        index = (u32)script_arg_num_range(call, 0, 0, data->animCount - 1);
  diag_assert(index < data->animCount);
  if (call->argCount < 2) {
    return script_num(data->anims[index].weight);
  }
  data->anims[index].weight = (f32)script_arg_num_range(call, 1, 0.0, 1.0);
  return script_null();
}

static ScriptVal import_eval_anim_mask(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data       = ctx->data;
  const u32        animIndex  = (u32)script_arg_num_range(call, 0, 0, data->animCount - 1);
  const u32        jointIndex = (u32)script_arg_num_range(call, 1, 0, data->jointCount - 1);
  diag_assert(animIndex < data->animCount);
  diag_assert(jointIndex < data->jointCount);
  if (call->argCount < 3) {
    return script_num(data->anims[animIndex].mask[jointIndex]);
  }
  data->anims[animIndex].mask[jointIndex] = (f32)script_arg_num_range(call, 2, 0.0, 1.0);
  return script_null();
}

static ScriptVal import_eval_anim_mask_all(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data      = ctx->data;
  const u32        animIndex = (u32)script_arg_num_range(call, 0, 0, data->animCount - 1);
  const f32        newWeight = (f32)script_arg_num_range(call, 1, 0.0, 1.0);
  diag_assert(animIndex < data->animCount);

  for (u32 jointIndex = 0; jointIndex != data->jointCount; ++jointIndex) {
    data->anims[animIndex].mask[jointIndex] = newWeight;
  }

  return script_null();
}

static ScriptVal import_eval_anim_mask_fade_up(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data        = ctx->data;
  const u32        animIdx     = (u32)script_arg_num_range(call, 0, 0, data->animCount - 1);
  const u32        jointIdx    = (u32)script_arg_num_range(call, 1, 0, data->jointCount - 1);
  const f32        deltaWeight = (f32)script_arg_num_range(call, 2, -1.0, +1.0);
  diag_assert(animIdx < data->animCount);
  diag_assert(jointIdx < data->jointCount);

  AssetImportAnim*  anim   = &data->anims[animIdx];
  AssetImportJoint* joints = data->joints;

  // Apply weight delta to jointIdx and all parents.
  f32 deltaSum = deltaWeight;
  for (u32 i = jointIdx;; i = joints[i].parentIndex) {
    anim->mask[i] = import_mesh_clamp01(anim->mask[i] + deltaSum);
    deltaSum += deltaWeight;
    if (joints[i].parentIndex == i) {
      break; // Reached the root.
    }
  }

  return script_null();
}

static ScriptVal import_eval_anim_mask_fade_down(AssetImportContext* ctx, ScriptBinderCall* call) {
  AssetImportMesh* data        = ctx->data;
  const u32        animIdx     = (u32)script_arg_num_range(call, 0, 0, data->animCount - 1);
  const u32        jointIdx    = (u32)script_arg_num_range(call, 1, 0, data->jointCount - 1);
  const f32        deltaWeight = (f32)script_arg_num_range(call, 2, -1.0, +1.0);
  diag_assert(animIdx < data->animCount);
  diag_assert(jointIdx < data->jointCount);

  AssetImportAnim*  anim   = &data->anims[animIdx];
  AssetImportJoint* joints = data->joints;

  // Apply weight delta to root.
  anim->mask[jointIdx] = import_mesh_clamp01(anim->mask[jointIdx] + deltaWeight);

  // Apply weight delta to children.
  const i32 parent                             = jointIdx ? (i32)joints[jointIdx].parentIndex : -1;
  u32       depthLookup[asset_mesh_joints_max] = {1};
  for (u32 i = jointIdx + 1; i != data->jointCount && (i32)joints[i].parentIndex > parent; ++i) {
    const u32 depth = depthLookup[i] = depthLookup[joints[i].parentIndex] + 1;
    anim->mask[i]                    = import_mesh_clamp01(anim->mask[i] + deltaWeight * depth);
  }

  return script_null();
}

void asset_data_init_import_mesh(void) {
  import_init_enum_anim_flags();

  const ScriptBinderFlags flags = ScriptBinderFlags_DisallowMemoryAccess;
  ScriptBinder* binder = script_binder_create(g_allocPersist, string_lit("import-mesh"), flags);
  script_binder_filter_set(binder, string_lit("import/mesh/*.script"));

  // clang-format off
  static const String g_animFlagsDoc = string_static("Supported flags:\n\n-`Active`\n\n-`Loop`\n\n-`FadeIn`\n\n-`FadeOut`\n\n-`RandomTime`");
  {
    const String       name   = string_lit("flat_normals");
    const String       doc    = fmt_write_scratch("Import flat (per face) normals (ignore per-vertex normals).");
    const ScriptMask   ret    = script_mask_bool | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("flatNormals"), script_mask_bool | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_flat_normals);
  }
  {
    const String       name   = string_lit("vertex_translation");
    const String       doc    = fmt_write_scratch("Set the vertex import translation.");
    const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("translation"), script_mask_vec3 | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_vertex_translation);
  }
  {
    const String       name   = string_lit("vertex_rotation");
    const String       doc    = fmt_write_scratch("Set the vertex import rotation.");
    const ScriptMask   ret    = script_mask_quat | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("rotation"), script_mask_quat | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_vertex_rotation);
  }
  {
    const String       name   = string_lit("vertex_scale");
    const String       doc    = fmt_write_scratch("Set the vertex import scale.");
    const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("scale"), script_mask_vec3 | script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_vertex_scale);
  }
  {
    const String       name   = string_lit("root_translation");
    const String       doc    = fmt_write_scratch("Set the bone root import translation (only valid for skinned meshes).");
    const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("translation"), script_mask_vec3 | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_root_translation);
  }
  {
    const String       name   = string_lit("root_rotation");
    const String       doc    = fmt_write_scratch("Set the bone root import rotation (only valid for skinned meshes).");
    const ScriptMask   ret    = script_mask_quat | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("rotation"), script_mask_quat | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_root_rotation);
  }
  {
    const String       name   = string_lit("root_scale");
    const String       doc    = fmt_write_scratch("Set the bone root import scale (only valid for skinned meshes).");
    const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("scale"), script_mask_vec3 | script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_root_scale);
  }
  {
    const String       name   = string_lit("joint_count");
    const String       doc    = fmt_write_scratch("Query the amount of joints in the mesh.\nThe joints are topologically sorted so the root is always at index 0.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_joint_count);
  }
  {
    const String       name   = string_lit("joint_parent");
    const String       doc    = fmt_write_scratch("Query the index of the joint's parent (same as the input for the root).");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_joint_parent);
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
  {
    const String       name   = string_lit("anim_count");
    const String       doc    = fmt_write_scratch("Query the amount of animations in the mesh.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    asset_import_bind(binder, name, doc, ret, null, 0, import_eval_anim_count);
  }
  {
    const String       name   = string_lit("anim_find");
    const String       doc    = fmt_write_scratch("Find an animation with the given name, returns the index of the animation or null if none was found.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("animName"), script_mask_str},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_find);
  }
  {
    const String       name   = string_lit("anim_layer");
    const String       doc    = fmt_write_scratch("Query or change the layer (sorting index) of the animation at the given index.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("newLayer"), script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_layer);
  }
  {
    const String       name   = string_lit("anim_flag");
    const String       doc    = fmt_write_scratch("Query or change an animation flag.\n\n{}", fmt_text(g_animFlagsDoc));
    const ScriptMask   ret    = script_mask_bool | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("flag"), script_mask_str},
        {string_lit("enable"), script_mask_bool | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_flag);
  }
  {
    const String       name   = string_lit("anim_name");
    const String       doc    = fmt_write_scratch("Query or change the name of the animation at the given index.");
    const ScriptMask   ret    = script_mask_str | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("newName"), script_mask_str | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_name);
  }
  {
    const String       name   = string_lit("anim_duration");
    const String       doc    = fmt_write_scratch("Query or change the animation duration.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("newDuration"), script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_duration);
  }
  {
    const String       name   = string_lit("anim_time");
    const String       doc    = fmt_write_scratch("Query or change the initial animation time (in seconds).");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("newTime"), script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_time);
  }
  {
    const String       name   = string_lit("anim_speed");
    const String       doc    = fmt_write_scratch("Query or change the initial animation speed.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("newSpeed"), script_mask_num | script_mask_null},
        {string_lit("newVariance"), script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_speed);
  }
  {
    const String       name   = string_lit("anim_weight");
    const String       doc    = fmt_write_scratch("Query or change the initial animation weight.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("newWeight"), script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_weight);
  }
  {
    const String       name   = string_lit("anim_mask");
    const String       doc    = fmt_write_scratch("Query or change the mask weight for a specific joint.");
    const ScriptMask   ret    = script_mask_num | script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("jointIndex"), script_mask_num},
        {string_lit("newWeight"), script_mask_num | script_mask_null},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_mask);
  }
  {
    const String       name   = string_lit("anim_mask_all");
    const String       doc    = fmt_write_scratch("Change the mask weight for all joints.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("newWeight"), script_mask_num},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_mask_all);
  }
  {
    const String       name   = string_lit("anim_mask_fade_up");
    const String       doc    = fmt_write_scratch("Recursively apply the weight delta to all joints up the hierarchy starting from the given joint.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("jointIndex"), script_mask_num},
        {string_lit("deltaWeight"), script_mask_num},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_mask_fade_up);
  }
  {
    const String       name   = string_lit("anim_mask_fade_down");
    const String       doc    = fmt_write_scratch("Recursively apply the weight delta to all joints down the hierarchy starting from the given joint.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("index"), script_mask_num},
        {string_lit("jointIndex"), script_mask_num},
        {string_lit("deltaWeight"), script_mask_num},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_anim_mask_fade_down);
  }
  // clang-format on

  asset_import_register(binder);

  script_binder_finalize(binder);
  g_assetScriptImportMeshBinder = binder;
}

bool asset_import_mesh(const AssetImportEnvComp* env, const String id, AssetImportMesh* data) {
  // Run import scripts.
  if (!asset_import_eval(env, g_assetScriptImportMeshBinder, id, data)) {
    return false;
  }

  // Check for duplicate joint names.
  StringHash duplicateJointNameHash;
  if (import_mesh_joint_find_duplicate(data, &duplicateJointNameHash)) {
    String duplicateJointName = string_lit("< unknown >");
    if (duplicateJointNameHash) {
      duplicateJointName = stringtable_lookup(g_stringtable, duplicateJointNameHash);
    }
    log_e(
        "Duplicate joint name found in mesh",
        log_param("asset", fmt_text(id)),
        log_param("joint-name", fmt_text(duplicateJointName)));
    return false;
  }

  // Check for duplicate animation names.
  StringHash duplicateAnimNameHash;
  if (import_mesh_anim_find_duplicate(data, &duplicateAnimNameHash)) {
    String duplicateAnimName = string_lit("< unknown >");
    if (duplicateAnimNameHash) {
      duplicateAnimName = stringtable_lookup(g_stringtable, duplicateAnimNameHash);
    }
    log_e(
        "Duplicate animation name found in mesh",
        log_param("asset", fmt_text(id)),
        log_param("anim-name", fmt_text(duplicateAnimName)));
    return false;
  }

  // Apply animation layer sorting.
  sort_quicksort_t(
      data->anims, data->anims + data->animCount, AssetImportAnim, import_compare_anim_layer);

  return true;
}
