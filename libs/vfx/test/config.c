#include "app/check.h"

void app_check_init(CheckDef* check) { register_spec(check, warp); }

void app_check_teardown(void) {}
