#pragma once
#include "check_def.h"

/**
 * Apis to be implemented by check applications.
 */

/**
 * Startup of the check application, use this to configure the test definition.
 * Use 'register_spec()' to register the test specifications.
 */
void app_check_init(CheckDef*);

/**
 * Teardown of the check application.
 */
void app_check_teardown(void);
