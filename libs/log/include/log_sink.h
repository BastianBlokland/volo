#include "log_logger.h"

struct sLogSink {

  /**
   * Function to call when a new message is written to the logger.
   */
  void (*write)(LogSink*, LogLevel, SourceLoc, TimeReal, String, const LogParam* params);

  /**
   * Function to call when the sink is destroyed.
   * Note: Can be 'null' when the sink doesn't need special destruction logic.
   */
  void (*destroy)(LogSink*);
};
