#if defined(VOLO_LINUX)
#include "device_pal_linux_alsa.c"
#elif defined(VOLO_WIN32)
#include "device_pal_win32_mme.c"
#else
#error Unsupported platform
#endif
