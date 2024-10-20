#pragma once
#include "data_registry.h"
#include "ecs_module.h"
#include "script_prog.h"

// Forward declare from 'script_binder.h'.
typedef struct sScriptBinder ScriptBinder;

/**
 * Script file.
 */
ecs_comp_extern_public(AssetScriptComp) {
  ScriptProgram prog;
  HeapArray_t(String) stringLiterals;
};

extern ScriptBinder* g_assetScriptBinder;
extern DataMeta      g_assetScriptMeta;

void asset_script_binder_write(DynString* str);
