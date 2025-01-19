#include "app_check.h"

void app_check_init(CheckDef* check) {
  register_spec(check, dynarray);
  register_spec(check, fizzbuzz);
}

void app_check_teardown(void) {}
