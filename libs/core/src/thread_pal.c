
#if defined(VOLO_LINUX)
#include "thread_pal_linux.c"
#elif defined(VOLO_WIN32)
#include "thread_pal_win32.c"
#else
_Static_assert(false, "Unsupported platform");
#endif
