#include "app/check.h"

void app_check_init(CheckDef* check) {
  register_spec(check, box_rotated);
  register_spec(check, box);
  register_spec(check, capsule);
  register_spec(check, color);
  register_spec(check, line);
  register_spec(check, matrix);
  register_spec(check, nav);
  register_spec(check, plane);
  register_spec(check, quat);
  register_spec(check, sphere);
  register_spec(check, vector);
}

void app_check_teardown(void) {}
