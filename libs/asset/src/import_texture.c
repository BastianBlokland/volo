#include "core_alloc.h"
#include "core_array.h"
#include "script_args.h"
#include "script_binder.h"
#include "script_sig.h"

#include "import_texture_internal.h"

ScriptBinder* g_assetScriptImportTextureBinder;

void asset_data_init_import_texture(void) {
  const ScriptBinderFlags flags = ScriptBinderFlags_DisallowMemoryAccess;
  ScriptBinder* binder = script_binder_create(g_allocPersist, string_lit("import-texture"), flags);
  script_binder_filter_set(binder, string_lit("import/texture/*.script"));

  asset_import_register(binder);

  script_binder_finalize(binder);
  g_assetScriptImportTextureBinder = binder;
}

bool asset_import_texture(const AssetImportEnvComp* env, const String id, AssetImportTexture* out) {
  *out = (AssetImportTexture){
      .dummy = 42,
  };
  return asset_import_eval(env, g_assetScriptImportTextureBinder, id, out);
}
