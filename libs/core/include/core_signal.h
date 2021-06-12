#pragma once
#include "core_types.h"

typedef enum {
  Signal_Interupt = 0,

  Signal_Count,
} Signal;

/**
 * Check if the current process has received a signal.
 */
bool signal_is_received(Signal);

/**
 * Reset a signal flag.
 * Will cause 'signal_is_received()' to return false until it is received again.
 */
void signal_reset(Signal);
