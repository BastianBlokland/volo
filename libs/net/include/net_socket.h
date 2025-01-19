#pragma once
#include "net.h"

typedef enum {
  NetSocketState_Idle,
  NetSocketState_Open,
  NetSocketState_Closed,
  NetSocketState_Error,
} NetSocketState;

/**
 * Network socket.
 * NOTE: Only TCP client sockets are supported.
 */
typedef struct sNetSocket NetSocket;

/**
 * Create a new socket.
 * Should be cleaned up using 'net_socket_destroy()'.
 */
NetSocket* net_socket_create(Allocator*);

/**
 * Destroy the given socket.
 */
void net_socket_destroy(NetSocket*);

/**
 * Query the state of the given socket.
 */
NetSocketState net_socket_state(const NetSocket*);

/**
 * Synchonously connect to a server at the given address.
 */
NetResult net_socket_connect_sync(NetSocket*, NetAddr);

/**
 * Synchronously write to the socket.
 */
NetResult net_socket_write_sync(NetSocket*, String);

/**
 * Synchronously read a block of available data in the dynamic-string.
 */
NetResult net_socket_read_sync(NetSocket*, DynString*);
