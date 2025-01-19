#include "app_check.h"

void app_check_init(CheckDef* check) {
  register_spec(check, dot);
  register_spec(check, executor);
  register_spec(check, graph);
  register_spec(check, scheduler);
}

void app_check_teardown(void) {}
