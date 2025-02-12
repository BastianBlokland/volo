#include "app_check.h"

void app_check_init(CheckDef* check) {
  register_spec(check, bin);
  register_spec(check, jsonschema);
  register_spec(check, read_json);
  register_spec(check, registry);
  register_spec(check, utils_clone);
  register_spec(check, utils_destroy);
  register_spec(check, utils_equal);
  register_spec(check, utils_hash);
  register_spec(check, utils_visit);
  register_spec(check, write_json);
}

void app_check_teardown(void) {}
