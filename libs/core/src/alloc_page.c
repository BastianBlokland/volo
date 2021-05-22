
#ifdef VOLO_LINUX
#include "alloc_page.linux.c"
#elif defined(VOLO_WIN32)
#include "alloc_page.win32.c"
#else
diag_static_assert(false, "Unsupported platform");
#endif
