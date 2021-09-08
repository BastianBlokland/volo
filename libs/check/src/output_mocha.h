#include "core_file.h"

#include "output.h"

/**
 * TODO:
 * Create a CheckOutput that writes pretty formatted text to the given file.
 * Note: the given file is automatically destroyed when the output is destroyed.
 * Destroy using 'check_output_destroy()'.
 */
CheckOutput* check_output_mocha(Allocator*, File*);

/**
 * TODO:
 */
CheckOutput* check_output_mocha_to_path(Allocator*, String path);

/**
 * TODO:
 */
CheckOutput* check_output_mocha_default(Allocator*);
