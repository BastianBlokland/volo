
#ifdef VOLO_LINUX
#include "alloc_page.linux.c"
#else
diag_static_assert(false, "Unsupported platform");
#endif
