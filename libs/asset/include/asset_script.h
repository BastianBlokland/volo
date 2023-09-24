#pragma once
#include "ecs_module.h"

// Forward declare from 'script_doc.h'.
typedef struct sScriptDoc ScriptDoc;
typedef u32               ScriptExpr;

/**
 * Script file.
 */

ecs_comp_extern_public(AssetScriptComp) {
  const ScriptDoc* doc;
  ScriptExpr       expr;
};
