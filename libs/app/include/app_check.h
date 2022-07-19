#pragma once
#include "check_def.h"

/**
 * Apis to be implemented by check applications.
 */

/**
 * Configure the check test definition.
 * Use 'register_spec()' to register the test specifications.
 */
void app_check_configure(CheckDef*);
