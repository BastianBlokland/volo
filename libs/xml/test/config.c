#include "app_check.h"

void app_check_init(CheckDef* check) { register_spec(check, doc); }

void app_check_teardown(void) {}
