#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, blackboard);
  register_spec(check, node_failure);
  register_spec(check, node_invert);
  register_spec(check, node_knowledgeclear);
  register_spec(check, node_knowledgeset);
  register_spec(check, node_parallel);
  register_spec(check, node_selector);
  register_spec(check, node_sequence);
  register_spec(check, node_success);
}
