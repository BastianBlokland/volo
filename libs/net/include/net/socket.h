#pragma once
#include "net/forward.h"

/**
 * Network socket.
 * NOTE: Only TCP client sockets are supported.
 */
typedef struct sNetSocket NetSocket;

/**
 * Synchonously open a Tcp connection to the given endpoint.
 * Should be cleaned up using 'net_socket_destroy()'.
 */
NetSocket* net_socket_connect_sync(Allocator*, NetEndpoint);
NetSocket* net_socket_connect_any_sync(Allocator*, const NetEndpoint* endpoints, u32 endpointCount);

/**
 * Destroy the given socket.
 */
void net_socket_destroy(NetSocket*);

/**
 * Query the status of the given socket.
 */
NetResult net_socket_status(const NetSocket*);

/**
 * Retrieve the remote endpoint of the socket.
 */
const NetEndpoint* net_socket_remote(const NetSocket*);

/**
 * Synchronously write to the socket.
 */
NetResult net_socket_write_sync(NetSocket*, String);

/**
 * Synchronously read a block of available data in the dynamic-string.
 */
NetResult net_socket_read_sync(NetSocket*, DynString* out);

/**
 * Shutdown the socket traffic in the specified direction.
 * NOTE: Can be called multiple times to shutdown different directions.
 */
NetResult net_socket_shutdown(NetSocket*, NetDir);
