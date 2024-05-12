#pragma once
#include "core_signal.h"

void signal_pal_setup_handlers(void);

i64  signal_pal_counter(Signal);
void signal_pal_reset(Signal);
