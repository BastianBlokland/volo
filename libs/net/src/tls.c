#if defined(VOLO_LINUX)
#include "tls_openssl.c"
#elif defined(VOLO_WIN32)
#include "tls_schannel.c"
#else
#error Unsupported platform
#endif
