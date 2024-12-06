#pragma once
#include "script_doc.h"

// Forward declare from 'script_binder.h'.
typedef struct sScriptBinder ScriptBinder;

// Forward declare from 'script_diag.h'.
typedef struct sScriptDiagBag ScriptDiagBag;

// Forward declare from 'script_sym.h'.
typedef struct sScriptSymBag ScriptSymBag;

/**
 * Read a script expression.
 * NOTE: If read fails then 'script_expr_sentinel' is returned.
 *
 * To receive string literals you can optionally provide a string-table.
 * To receive diagnostics you can optionally provide a diagnostic-bag.
 */
ScriptExpr script_read(
    ScriptDoc*, const ScriptBinder*, String src, StringTable*, ScriptDiagBag*, ScriptSymBag*);
