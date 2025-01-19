#include "app_check.h"

void app_check_init(CheckDef* check) {
  register_spec(check, nav);
  register_spec(check, script);
  register_spec(check, set);
}

void app_check_teardown(void) {}
