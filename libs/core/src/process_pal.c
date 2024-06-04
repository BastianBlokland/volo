#if defined(VOLO_LINUX)
#include "process_pal_linux.c"
#elif defined(VOLO_WIN32)
#include "process_pal_win32.c"
#else
#error Unsupported platform
#endif
