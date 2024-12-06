#pragma once
#include "core.h"

typedef enum eSignal {
  Signal_Terminate,
  Signal_Interrupt,
  Signal_Kill, // Not interceptable.

  Signal_Count,
} Signal;

/**
 * Enable signal interception.
 * NOTE: This suppresses the default system signal handling.
 */
void signal_intercept_enable(void);

/**
 * Check if the current process has received a signal.
 * NOTE: Requires interception to be enabled by calling 'signal_intercept_enable()'.
 */
bool signal_is_received(Signal);

/**
 * Reset a signal flag.
 * Will cause 'signal_is_received()' to return false until it is received again.
 */
void signal_reset(Signal);
