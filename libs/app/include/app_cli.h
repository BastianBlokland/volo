#pragma once
#include "cli.h"
#include "core_string.h"
#include "core_types.h"

/**
 * Apis to be implemented by cli applications.
 */

String app_cli_desc();
void   app_cli_configure(CliApp*);
i32    app_cli_run(const CliApp*, const CliInvocation*);
