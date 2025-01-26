#pragma once
#include "net.h"

typedef enum {
  NetHttpFlags_None        = 0,
  NetHttpFlags_Tls         = 1 << 0,                    // Https.
  NetHttpFlags_TlsNoVerify = NetHttpFlags_Tls | 1 << 1, // Https without Tls cert verification.
} NetHttpFlags;

/**
 * Http (Hypertext Transfer Protocol) connection.
 */
typedef struct sNetHttp NetHttp;

/**
 * Establish a Http connection to a remote server.
 * NOTE: Multiple requests can be made serially over the same connection.
 * Should be cleaned up using 'net_http_destroy()'.
 */
NetHttp* net_http_connect_sync(Allocator*, String host, NetHttpFlags);

/**
 * Destroy the given Http connection.
 * NOTE: For gracefully ended sessions call 'net_http_shutdown_sync()' before destroying.
 */
void net_http_destroy(NetHttp*);

/**
 * Query the status of the given Http connection.
 */
NetResult net_http_status(const NetHttp*);

/**
 * Query information about the remote server.
 */
const NetAddr* net_http_remote(const NetHttp*);
String         net_http_remote_name(const NetHttp*);

/**
 * Synchonously perform a 'GET' request for the given resource.
 * NOTE: Response body is written to the output DynString.
 */
NetResult net_http_get_sync(NetHttp*, String uri, DynString* out);

/**
 * Synchonously shutdown the Http connection.
 */
NetResult net_http_shutdown_sync(NetHttp*);
