#pragma once
#include "data_registry.h"
#include "ecs_module.h"
#include "script_prog.h"

typedef enum {
  AssetScriptDomain_ImportMesh,
  AssetScriptDomain_ImportTexture,
  AssetScriptDomain_Scene,
} AssetScriptDomain;

/**
 * Script file.
 */
ecs_comp_extern_public(AssetScriptComp) {
  AssetScriptDomain domain;
  u32               hash;
  ScriptProgram     prog;
  HeapArray_t(StringHash) inputKeys; // Input memory keys.
  HeapArray_t(StringHash) strings;   // All strings used by the script.
};

extern ScriptBinder* g_assetScriptImportMeshBinder;
extern ScriptBinder* g_assetScriptImportTextureBinder;
extern ScriptBinder* g_assetScriptSceneBinder;
extern DataMeta      g_assetScriptMeta;
