#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, affinity);
  register_spec(check, combinator);
  register_spec(check, def);
  register_spec(check, destruct);
  register_spec(check, entity);
  register_spec(check, graph);
  register_spec(check, runner);
  register_spec(check, storage);
  register_spec(check, utils);
  register_spec(check, view);
  register_spec(check, world);
}
