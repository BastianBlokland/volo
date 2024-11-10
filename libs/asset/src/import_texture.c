#include "core_alloc.h"
#include "core_array.h"
#include "script_args.h"
#include "script_binder.h"
#include "script_enum.h"
#include "script_sig.h"

#include "import_texture_internal.h"

ScriptBinder* g_assetScriptImportTextureBinder;

static ScriptEnum g_assetImportTextureFlags;

static void import_init_enum_flags(void) {
#define PUSH_FLAG(_ENUM_, _NAME_)                                                                  \
  script_enum_push((_ENUM_), string_lit(#_NAME_), AssetImportTextureFlags_##_NAME_);

  PUSH_FLAG(&g_assetImportTextureFlags, NormalMap);
  PUSH_FLAG(&g_assetImportTextureFlags, Lossless);
  PUSH_FLAG(&g_assetImportTextureFlags, Linear);

#undef PUSH_FLAG
}

void asset_data_init_import_texture(void) {
  import_init_enum_flags();

  const ScriptBinderFlags flags = ScriptBinderFlags_DisallowMemoryAccess;
  ScriptBinder* binder = script_binder_create(g_allocPersist, string_lit("import-texture"), flags);
  script_binder_filter_set(binder, string_lit("import/texture/*.script"));

  asset_import_register(binder);

  script_binder_finalize(binder);
  g_assetScriptImportTextureBinder = binder;
}

bool asset_import_texture(const AssetImportEnvComp* env, const String id, AssetImportTexture* out) {
  *out = (AssetImportTexture){0};
  return asset_import_eval(env, g_assetScriptImportTextureBinder, id, out);
}
