#pragma once
#include "net.h"

typedef u32 NetRestId;

/**
 * Rest (REpresentational State Transfer) session.
 */
typedef struct sNetRest NetRest;

/**
 * Create a rest session.
 * Should be cleaned up using 'net_rest_destroy()'.
 */
NetRest* net_rest_create(Allocator*, u32 workerCount, u32 requestCount, NetHttpFlags);

/**
 * Destroy the given rest session.
 */
void net_rest_destroy(NetRest*);

/**
 * Start a new rest request.
 * NOTE: Requests need to be released using 'net_rest_release()'.
 */
NetRestId net_rest_head(NetRest*, String host, String uri, const NetHttpAuth*, const NetHttpEtag*);
NetRestId net_rest_get(NetRest*, String host, String uri, const NetHttpAuth*, const NetHttpEtag*);

/**
 * Query request status.
 */
bool               net_rest_done(NetRest*, NetRestId);
NetResult          net_rest_result(NetRest*, NetRestId);
String             net_rest_data(NetRest*, NetRestId);
const NetHttpEtag* net_rest_etag(NetRest*, NetRestId);
bool               net_rest_release(NetRest*, NetRestId);
