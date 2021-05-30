#include "core_tty.h"
#include "file_internal.h"
#include <Windows.h>

void tty_pal_init() {}

bool tty_isatty(File* file) { return GetFileType(file->handle) == FILE_TYPE_CHAR; }
