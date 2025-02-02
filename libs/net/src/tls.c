#if defined(VOLO_LINUX)
#include "tls_openssl.c"
#elif defined(VOLO_WIN32)
#include "tls_schannel.c" // NOTE: If desired openssl can used on Windows instead of SChannel.
#else
#error Unsupported platform
#endif
