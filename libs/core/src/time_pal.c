
#ifdef VOLO_LINUX
#include "time_pal_linux.c"
#elif defined(VOLO_WIN32)
#include "time_pal_win32.c"
#else
diag_static_assert(false, "Unsupported platform");
#endif
