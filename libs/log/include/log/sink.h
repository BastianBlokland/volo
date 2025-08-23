#pragma once
#include "log/logger.h"

struct sLogSink {

  /**
   * Function to call when a new message is written to the logger.
   * NOTE: Function can be invoked from different threads in parallel.
   */
  void (*write)(LogSink*, LogLevel, SourceLoc, TimeReal, String, const LogParam* params);

  /**
   * Function to call when the sink is destroyed.
   * NOTE: Can be 'null' when the sink doesn't need special destruction logic.
   */
  void (*destroy)(LogSink*);
};
