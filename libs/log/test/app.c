#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, logger);
  register_spec(check, sink_json);
  register_spec(check, sink_pretty);
}
