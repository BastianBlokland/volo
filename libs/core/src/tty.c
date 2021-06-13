#include "init_internal.h"
#include "tty_internal.h"

void tty_init() { tty_pal_init(); }
void tty_teardown() { tty_pal_teardown(); }
