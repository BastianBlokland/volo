#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, blackboard);
  register_spec(check, node_failure);
  register_spec(check, node_success);
}
