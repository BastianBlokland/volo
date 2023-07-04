#include "core_annotation.h"

#if defined(VOLO_LINUX)
#include "file_iterator_pal_linux.c"
#elif defined(VOLO_WIN32)
#include "file_iterator_pal_win32.c"
#else
ASSERT(false, "Unsupported platform");
#endif
