#pragma once
#include "script_doc.h"

// Forward declare from 'core_binder.h'.
typedef struct sScriptBinder ScriptBinder;

// Forward declare from 'core_diag.h'.
typedef struct sScriptDiagBag ScriptDiagBag;

/**
 * Read a script expression.
 * NOTE: If read fails then 'script_expr_sentinel' is returned.
 *
 * To receive diagnostics you can optionally provide a diagnostic-bag.
 */
ScriptExpr script_read(ScriptDoc*, const ScriptBinder*, String sourceText, ScriptDiagBag*);
