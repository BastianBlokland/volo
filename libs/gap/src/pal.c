#include "core_annotation.h"

#if defined(VOLO_LINUX)
#include "pal_linux_xcb.c"
#elif defined(VOLO_WIN32)
#include "pal_win32.c"
#else
ASSERT(false, "Unsupported platform");
#endif
