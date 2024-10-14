#pragma once
#include "script_doc.h"

/**
 * Perform various optimization passes on the given expression.
 * NOTE: May modify the existing expression or return a new expression.
 */
ScriptExpr script_optimize(ScriptDoc*, ScriptExpr);
