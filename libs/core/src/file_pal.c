#if defined(VOLO_LINUX)
#include "file_pal_linux.c"
#elif defined(VOLO_WIN32)
#include "file_pal_win32.c"
#else
#error Unsupported platform
#endif
