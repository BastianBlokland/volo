#pragma once
#include "net.h"

typedef enum {
  NetHttpFlags_None        = 0,
  NetHttpFlags_Tls         = 1 << 0,                    // Https.
  NetHttpFlags_TlsNoVerify = NetHttpFlags_Tls | 1 << 1, // Https without Tls cert verification.
} NetHttpFlags;

/**
 * TODO:
 */
typedef struct sNetHttp NetHttp;

/**
 * TODO:
 */
NetHttp* net_http_connect_sync(Allocator*, String host, NetHttpFlags);

/**
 * TODO:
 */
void net_http_destroy(NetHttp*);

/**
 * TODO:
 */
NetResult net_http_status(const NetHttp*);

/**
 * TODO:
 */
const NetAddr* net_http_remote(const NetHttp*);
String         net_http_remote_name(const NetHttp*);

/**
 * TODO:
 */
NetResult net_http_get_sync(NetHttp*, String uri, DynString* out);

/**
 * TODO:
 */
NetResult net_http_shutdown_sync(NetHttp*);
