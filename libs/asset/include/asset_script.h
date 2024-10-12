#pragma once
#include "ecs_module.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

// Forward declare from 'script_binder.h'.
typedef struct sScriptBinder ScriptBinder;

// Forward declare from 'script_doc.h'.
typedef struct sScriptDoc ScriptDoc;
typedef u32               ScriptExpr;

/**
 * Script file.
 */
ecs_comp_extern_public(AssetScriptComp) {
  String           sourceText; // Used for reporting error positions.
  const ScriptDoc* doc;
  ScriptExpr       expr;
  String           code;
};

extern ScriptBinder* g_assetScriptBinder;

void asset_script_binder_write(DynString* str);
