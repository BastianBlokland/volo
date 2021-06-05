#include "core_diag.h"

#ifdef VOLO_LINUX
#include "path_pal_linux.c"
#elif defined(VOLO_WIN32)
#include "path_pal_win32.c"
#else
diag_static_assert(false, "Unsupported platform");
#endif
