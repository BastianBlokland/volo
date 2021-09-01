#pragma once
#include "core_dynstring.h"

/**
 * Read an envrionment variable from the process environment.
 * Return value indicates if the variable was found.
 *
 * When true is returned the value of the environment variable is written to the output
 * dynamic-string, pass null to ignore the value (usefull for just checking if a variable exists).
 *
 * Pre-condition: name.size < 256.
 */
bool env_var(String name, DynString* output);
