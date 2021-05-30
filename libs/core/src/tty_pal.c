#include "core_diag.h"

#ifdef VOLO_LINUX
#include "tty_pal_linux.c"
#elif defined(VOLO_WIN32)
#include "tty_pal_win32.c"
#else
diag_static_assert(false, "Unsupported platform");
#endif
