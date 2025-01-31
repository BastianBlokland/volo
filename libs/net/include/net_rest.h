#pragma once
#include "net.h"

#define net_rest_workers_max 8

/**
 * Rest (REpresentational State Transfer) session.
 */
typedef struct sNetRest NetRest;

/**
 * Create a rest session.
 * Should be cleaned up using 'net_rest_destroy()'.
 */
NetRest* net_rest_create(Allocator*, u32 workerCount);

/**
 * Destroy the given rest session.
 */
void net_rest_destroy(NetRest*);
