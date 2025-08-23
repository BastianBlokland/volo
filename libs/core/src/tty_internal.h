#pragma once
#include "core/tty.h"

void tty_pal_init(void);
void tty_pal_teardown(void);

bool tty_pal_isatty(File*);
u16  tty_pal_width(File*);
u16  tty_pal_height(File*);
void tty_pal_opts_set(File*, TtyOpts);
bool tty_pal_read(File*, DynString*, TtyReadFlags);
