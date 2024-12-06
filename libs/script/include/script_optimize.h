#pragma once
#include "script.h"

/**
 * Perform various optimization passes on the given expression.
 * NOTE: Returns a new optimized expression or the same expression if no optimization was possible.
 */
ScriptExpr script_optimize(ScriptDoc*, ScriptExpr);
