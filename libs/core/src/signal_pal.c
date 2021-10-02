#include "core_annotation.h"

#if defined(VOLO_LINUX)
#include "signal_pal_linux.c"
#elif defined(VOLO_WIN32)
#include "signal_pal_win32.c"
#else
ASSERT(false, "Unsupported platform");
#endif
