#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, blackboard);
  register_spec(check, node_failure);
  register_spec(check, node_invert);
  register_spec(check, node_knowledgecompare);
  register_spec(check, node_knowledgeset);
  register_spec(check, node_parallel);
  register_spec(check, node_repeat);
  register_spec(check, node_running);
  register_spec(check, node_selector);
  register_spec(check, node_sequence);
  register_spec(check, node_success);
  register_spec(check, node_try);
  register_spec(check, tracer_record);
  register_spec(check, value);
}
