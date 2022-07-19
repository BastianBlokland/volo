#pragma once
#include "cli.h"
#include "core_string.h"
#include "core_types.h"

/**
 * Apis to be implemented by cli applications.
 */

/**
 * Configure the CommandLineInterface application.
 * Use the various 'cli_register_*' apis from the cli_app.h header.
 */
void app_cli_configure(CliApp*);

/**
 * Run the application logic.
 * Return value is the application exit-code.
 */
i32 app_cli_run(const CliApp*, const CliInvocation*);
