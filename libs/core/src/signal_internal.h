#pragma once
#include "core_signal.h"

void signal_pal_setup_handlers();

bool signal_pal_is_received(Signal);
void signal_pal_reset(Signal);
