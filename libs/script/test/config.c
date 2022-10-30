#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, lex);
  register_spec(check, val);
}
