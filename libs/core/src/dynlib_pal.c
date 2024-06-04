#if defined(VOLO_LINUX)
#include "dynlib_pal_linux.c"
#elif defined(VOLO_WIN32)
#include "dynlib_pal_win32.c"
#else
#error Unsupported platform
#endif
