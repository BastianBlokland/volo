#include "output.h"

void check_output_destroy(CheckOutput* out) { out->destroy(out); }
