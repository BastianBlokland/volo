#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, read_json);
  register_spec(check, registry);
  register_spec(check, utils_clone);
  register_spec(check, utils_destroy);
  register_spec(check, write_json);
}
