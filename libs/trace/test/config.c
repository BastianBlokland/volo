#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, dump_eventtrace);
  register_spec(check, sink_store);
  register_spec(check, tracer);
}
