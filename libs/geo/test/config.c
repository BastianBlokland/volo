#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, box);
  register_spec(check, line);
  register_spec(check, matrix);
  register_spec(check, nav);
  register_spec(check, plane);
  register_spec(check, quat);
  register_spec(check, sphere);
  register_spec(check, vector);
}
