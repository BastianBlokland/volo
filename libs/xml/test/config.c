#include "app_check.h"

void app_check_init(CheckDef* check) {
  register_spec(check, doc);
  register_spec(check, eq);
}

void app_check_teardown(void) {}
