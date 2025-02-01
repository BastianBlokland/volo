#include "core_alloc.h"
#include "core_math.h"
#include "net_rest.h"

typedef struct {
  u32 dummy;
} NetRestWorker;

typedef struct {
  u32 dummy;
} NetRestRequest;

typedef struct sNetRest {
  Allocator*      alloc;
  NetRestWorker*  workers;
  NetRestRequest* requests;
  u32             workerCount, requestCount;
} NetRest;

NetRest* net_rest_create(Allocator* alloc, u32 workerCount, u32 requestCount) {
  workerCount  = math_max(1, workerCount);
  requestCount = math_max(workerCount, requestCount);

  NetRest* rest = alloc_alloc_t(alloc, NetRest);

  *rest = (NetRest){
      .alloc        = alloc,
      .workers      = alloc_array_t(alloc, NetRestWorker, workerCount),
      .requests     = alloc_array_t(alloc, NetRestRequest, requestCount),
      .workerCount  = workerCount,
      .requestCount = requestCount,
  };

  return rest;
}

void net_rest_destroy(NetRest* rest) {
  alloc_free_array_t(rest->alloc, rest->workers, rest->workerCount);
  alloc_free_array_t(rest->alloc, rest->requests, rest->requestCount);
  alloc_free_t(rest->alloc, rest);
}
