#include "app/check.h"
#include "net/init.h"

void app_check_init(CheckDef* check) {
  net_init();

  register_spec(check, addr);
  register_spec(check, http);
  register_spec(check, socket);
}

void app_check_teardown(void) { net_teardown(); }
