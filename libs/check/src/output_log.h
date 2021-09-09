#include "log_logger.h"

#include "output.h"

/**
 * Create a CheckOutput that writes log messages to the given logger.
 * Destroy using 'check_output_destroy()'.
 */
CheckOutput* check_output_log(Allocator*, Logger*);
