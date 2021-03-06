#include "core_file.h"

#include "output_internal.h"

/**
 * Create a CheckOutput that writes pretty formatted text to the given file.
 * Destroy using 'check_output_destroy()'.
 */
CheckOutput* check_output_pretty(Allocator*, File*, CheckRunFlags);
