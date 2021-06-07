#include "core_tty.h"
#include "file_internal.h"
#include <unistd.h>

void tty_pal_init() {}
void tty_pal_teardown() {}

bool tty_isatty(File* file) { return isatty(file->handle); }
