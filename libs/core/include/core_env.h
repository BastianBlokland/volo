#pragma once
#include "core.h"

/**
 * Read an envrionment variable from the process environment.
 * Return value indicates if the variable was found.
 *
 * When true is returned the value of the environment variable is written to the output
 * dynamic-string, pass null to ignore the value (useful for just checking if a variable exists).
 *
 * Pre-condition: name.size < 256.
 */
bool   env_var(String name, DynString* output);
String env_var_scratch(String name);
