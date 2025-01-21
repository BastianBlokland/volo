#pragma once
#include "net.h"

/**
 * Tls (Transport Layer Security) aka Ssl (Secure Sockets Layer).
 * Provides a secure connection channel on top of an underlying transport.
 */

typedef enum {
  NetTlsFlags_None,
  NetTlsFlags_NoVerify = 1 << 0, // Do not verify certificates.
} NetTlsFlags;

/**
 * Tls session to a remote peer.
 * NOTE: Session cannot be reused (nether with different or with the same peer).
 */
typedef struct sNetTls NetTls;

/**
 * Create a new Tls session.
 * NOTE: Tls handshake is transparently performed on the first read / write.
 */
NetTls* net_tls_create(Allocator*, NetTlsFlags);

/**
 * Destroy the given Tls object.
 */
void net_tls_destroy(NetTls*);

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

/**
 * Synchonously shutdown the Tls session.
 */
NetResult net_tls_shutdown_sync(NetTls*, NetSocket*);
