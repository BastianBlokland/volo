#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, brain);
  register_spec(check, nav);
}
