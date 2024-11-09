#pragma once
#include "ecs_module.h"

// Forward declare from 'script_binder.h'.
typedef struct sScriptBinder     ScriptBinder;
typedef struct sScriptBinderCall ScriptBinderCall;

// Forward declare from 'script_val.h'.
typedef struct sScriptVal ScriptVal;
typedef u16               ScriptMask;

// Forward declare from 'script_sig.h'.
typedef struct sScriptSigArg ScriptSigArg;

// Forward declare from 'script_prog.h'.
typedef struct sScriptProgram ScriptProgram;

/**
 * Global asset import environment.
 */
ecs_comp_extern(AssetImportEnvComp);

/**
 * Check if we are ready to import an asset with the given id.
 */
bool asset_import_ready(const AssetImportEnvComp*, String assetId);

/**
 * Lookup the import hash of an asset with the given id. When the import hash changes the asset has
 * to be re-imported.
 * Pre-condition: asset_import_ready().
 */
u32 asset_import_hash(const AssetImportEnvComp*, String assetId);

/**
 * Register generic script bindings.
 */
void asset_import_register(ScriptBinder*);

typedef struct {
  String               assetId;
  const ScriptProgram* prog;
  String               progId;
  void*                out; // Type specific output data.
} AssetImportContext;

typedef ScriptVal (*AssetImportBinderFunc)(AssetImportContext*, ScriptBinderCall*);

void asset_import_bind(
    ScriptBinder*,
    String              name,
    String              doc,
    ScriptMask          retMask,
    const ScriptSigArg* args,
    u8                  argCount,
    AssetImportBinderFunc);

bool asset_import_eval(const AssetImportEnvComp*, const ScriptBinder*, String assetId, void* out);
