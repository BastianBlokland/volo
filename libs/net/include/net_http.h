#pragma once
#include "core_string.h"
#include "net.h"

typedef enum eNetHttpFlags {
  NetHttpFlags_None        = 0,
  NetHttpFlags_Tls         = 1 << 0,                    // Https.
  NetHttpFlags_TlsNoVerify = NetHttpFlags_Tls | 1 << 1, // Https without Tls cert verification.
} NetHttpFlags;

typedef enum {
  NetHttpAuthType_None,
  NetHttpAuthType_Basic,
} NetHttpAuthType;

typedef struct sNetHttpAuth {
  NetHttpAuthType type;
  String          user, pw;
} NetHttpAuth;

NetHttpAuth net_http_auth_clone(const NetHttpAuth*, Allocator*);
void        net_http_auth_free(NetHttpAuth*, Allocator*);

typedef struct sNetHttpEtag {
  u8 length;
  u8 data[63];
} NetHttpEtag;

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
const NetEndpoint* net_http_remote(const NetHttp*);
String             net_http_remote_name(const NetHttp*);

/**
 * Synchonously perform a 'HEAD' request for the given resource.
 */
NetResult net_http_head_sync(NetHttp*, String uri, const NetHttpAuth*, NetHttpEtag*);

/**
 * Synchonously perform a 'GET' request for the given resource.
 * NOTE: Response body is written to the output DynString.
 */
NetResult net_http_get_sync(NetHttp*, String uri, const NetHttpAuth*, NetHttpEtag*, DynString* out);

/**
 * Synchonously shutdown the Http connection.
 */
NetResult net_http_shutdown_sync(NetHttp*);
