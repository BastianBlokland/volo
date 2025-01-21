#pragma once
#include "net.h"

typedef enum {
  NetTlsFlags_None,
  NetTlsFlags_NoVerify = 1 << 0, // Do not verify certificates.
} NetTlsFlags;

/**
 * TODO:
 */
typedef struct sNetTls NetTls;

/**
 * TODO:
 * NOTE: Tls object represents a single session and cannot be reused.
 * Should be cleaned up using 'net_tls_destroy()'.
 */
NetTls* net_tls_create(Allocator*, NetTlsFlags);

/**
 * Destroy the given Tls object.
 * NOTE: Optionally provide a socket to perform proper Tls shutdown (will notify the remote).
 */
void net_tls_destroy(NetTls*, NetSocket*);

/**
 * Query the status of the given Tls object.
 */
NetResult net_tls_status(const NetTls*);

/**
 * Synchronously write to the tls-object.
 */
NetResult net_tls_write_sync(NetTls*, NetSocket*, String);

/**
 * Synchronously read a block of available data in the dynamic-string.
 */
NetResult net_tls_read_sync(NetTls*, NetSocket*, DynString*);
