#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, app);
  register_spec(check, failure);
  register_spec(check, help);
  register_spec(check, parse);
  register_spec(check, read);
  register_spec(check, validate);
}
