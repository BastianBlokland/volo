#include "init_internal.h"
#include "signal_internal.h"

void signal_init() { signal_pal_setup_handlers(); }

bool signal_is_received(Signal sig) { return signal_pal_is_received(sig); }

void signal_reset(Signal sig) { signal_pal_reset(sig); }
