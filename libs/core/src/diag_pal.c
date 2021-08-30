
#if defined(VOLO_LINUX)
#include "diag_pal_linux.c"
#elif defined(VOLO_WIN32)
#include "diag_pal_win32.c"
#else
_Static_assert(false, "Unsupported platform");
#endif
