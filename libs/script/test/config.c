#include "app_check.h"

void app_check_init(CheckDef* check) {
  register_spec(check, binder);
  register_spec(check, doc);
  register_spec(check, enum_);
  register_spec(check, eval);
  register_spec(check, format);
  register_spec(check, lex);
  register_spec(check, mem);
  register_spec(check, optimize);
  register_spec(check, prog);
  register_spec(check, read);
  register_spec(check, sig);
  register_spec(check, val);
}

void app_check_teardown(void) {}
