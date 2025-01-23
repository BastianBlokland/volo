#pragma once
#include "net.h"

typedef enum {
  NetHttpProtocol_Http,
  NetHttpProtocol_Https,
} NetHttpProtocol;

/**
 * TODO:
 */
typedef struct sNetHttp NetHttp;

/**
 * TODO:
 */
NetHttp* net_http_connect_sync(Allocator*, NetHttpProtocol, String host);

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
NetResult net_http_get_sync(NetHttp*, String resource, DynString* out);

/**
 * TODO:
 */
NetResult net_http_shutdown_sync(NetHttp*);
