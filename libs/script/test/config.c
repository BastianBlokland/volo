#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, compare);
  register_spec(check, doc);
  register_spec(check, lex);
  register_spec(check, read);
  register_spec(check, val);
}
