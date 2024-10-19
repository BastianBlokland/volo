#pragma once
#include "ecs_module.h"
#include "script_prog.h"

// Forward declare from 'script_binder.h'.
typedef struct sScriptBinder ScriptBinder;

/**
 * Script file.
 */
ecs_comp_extern_public(AssetScriptComp) { ScriptProgram prog; };

extern ScriptBinder* g_assetScriptBinder;

void asset_script_binder_write(DynString* str);
