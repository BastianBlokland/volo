#include "app_check.h"

void app_check_init(CheckDef* check) {
  register_spec(check, logger);
  register_spec(check, sink_json);
  register_spec(check, sink_pretty);
}

void app_check_teardown(void) {}
