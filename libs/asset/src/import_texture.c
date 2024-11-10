#include "core_alloc.h"
#include "core_array.h"
#include "core_format.h"
#include "script_args.h"
#include "script_binder.h"
#include "script_enum.h"
#include "script_sig.h"

#include "import_texture_internal.h"

ScriptBinder* g_assetScriptImportTextureBinder;

static ScriptEnum g_importTextureFlags;

static void import_init_enum_flags(void) {
#define PUSH_FLAG(_ENUM_, _NAME_)                                                                  \
  script_enum_push((_ENUM_), string_lit(#_NAME_), AssetImportTextureFlags_##_NAME_);

  PUSH_FLAG(&g_importTextureFlags, NormalMap);
  PUSH_FLAG(&g_importTextureFlags, Lossless);
  PUSH_FLAG(&g_importTextureFlags, Linear);

#undef PUSH_FLAG
}

static ScriptVal import_eval_texture_flag(AssetImportContext* ctx, ScriptBinderCall* call) {
  const i32  flag    = script_arg_enum(call, 0, &g_importTextureFlags);
  const bool enabled = script_arg_bool(call, 1);
  if (!script_call_panicked(call)) {
    AssetImportTexture* out = ctx->out;
    if (enabled != !!(out->flags & flag)) {
      out->flags ^= flag;
    }
  }
  return script_null();
}

void asset_data_init_import_texture(void) {
  import_init_enum_flags();

  const ScriptBinderFlags flags = ScriptBinderFlags_DisallowMemoryAccess;
  ScriptBinder* binder = script_binder_create(g_allocPersist, string_lit("import-texture"), flags);
  script_binder_filter_set(binder, string_lit("import/texture/*.script"));

  // clang-format off
  static const String g_flagsDoc = string_static("Supported flags:\n\n-`NormalMap`\n\n-`Lossless`\n\n-`Linear`");
   {
    const String       name   = string_lit("texture_flag");
    const String       doc    = fmt_write_scratch("Set or unset a texture import flag.\n\n{}", fmt_text(g_flagsDoc));
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("flag"), script_mask_str},
        {string_lit("enable"), script_mask_bool},
    };
    asset_import_bind(binder, name, doc, ret, args, array_elems(args), import_eval_texture_flag);
  }
  // clang-format on

  asset_import_register(binder);

  script_binder_finalize(binder);
  g_assetScriptImportTextureBinder = binder;
}

bool asset_import_texture(const AssetImportEnvComp* env, const String id, AssetImportTexture* out) {
  *out = (AssetImportTexture){0};
  return asset_import_eval(env, g_assetScriptImportTextureBinder, id, out);
}