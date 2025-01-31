#include "core_alloc.h"
#include "net_rest.h"

typedef struct sNetRest {
  Allocator* alloc;
  u32        workers[net_rest_workers_max];
  u32        workerCount;
} NetRest;

NetRest* net_rest_create(Allocator* alloc, const u32 workerCount) {
  NetRest* rest = alloc_alloc_t(alloc, NetRest);
  *rest         = (NetRest){.alloc = alloc, .workerCount = workerCount};
  return rest;
}

void net_rest_destroy(NetRest* rest) { alloc_free_t(rest->alloc, rest); }
