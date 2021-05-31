#include "core_tty.h"

void tty_pal_init();
void tty_pal_teardown();

void tty_init() { tty_pal_init(); }
void tty_teardown() { tty_pal_teardown(); }
