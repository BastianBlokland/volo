#if defined(VOLO_LINUX)
#include "pal_linux_xcb.c"
#elif defined(VOLO_WIN32)
#include "pal_win32.c"
#else
#error Unsupported platform
#endif
