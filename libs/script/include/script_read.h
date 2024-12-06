#pragma once
#include "script.h"

/**
 * Read a script expression.
 * NOTE: If read fails then 'script_expr_sentinel' is returned.
 *
 * To receive string literals you can optionally provide a string-table.
 * To receive diagnostics you can optionally provide a diagnostic-bag.
 */
ScriptExpr script_read(
    ScriptDoc*, const ScriptBinder*, String src, StringTable*, ScriptDiagBag*, ScriptSymBag*);
