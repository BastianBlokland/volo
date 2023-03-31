#include "core_annotation.h"

#if defined(VOLO_LINUX)
#include "device_pal_linux_alsa.c"
#elif defined(VOLO_WIN32)
#include "device_pal_win32_waveout.c"
#else
ASSERT(false, "Unsupported platform");
#endif
