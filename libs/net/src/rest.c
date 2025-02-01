#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_format.h"
#include "core_math.h"
#include "core_thread.h"
#include "net_http.h"
#include "net_rest.h"
#include "net_result.h"

typedef enum {
  NetRestState_Idle,
  NetRestState_Acquired,
  NetRestState_Ready,
  NetRestState_Busy,
  NetRestState_Finished,
} NetRestState;

typedef struct {
  NetRestState state;
  NetResult    result;
  u16          generation;
  String       host, uri;
  NetHttpAuth  auth;
  NetHttpEtag  etag;
  DynString    buffer;
} NetRestRequest;

typedef struct sNetRest {
  Allocator* alloc;

  ThreadMutex     workerMutex;
  ThreadCondition workerWakeCondition;
  ThreadHandle*   workerThreads;
  bool            workerShutdown;
  u32             workerCount;

  NetRestRequest* requests;
  u32             requestCount;
} NetRest;

static void rest_wake_worker_all(NetRest* rest) {
  thread_mutex_lock(rest->workerMutex);
  thread_cond_broadcast(rest->workerWakeCondition);
  thread_mutex_unlock(rest->workerMutex);
}

static void rest_wake_worker_single(NetRest* rest) {
  thread_mutex_lock(rest->workerMutex);
  thread_cond_signal(rest->workerWakeCondition);
  thread_mutex_unlock(rest->workerMutex);
}

static u16 rest_id_index(const NetRestId id) { return (u16)id; }
static u16 rest_id_generation(const NetRestId id) { return (u16)(id >> 16); }

static NetRestId rest_id_create(const u16 index, const u16 generation) {
  return (NetRestId)index | ((NetRestId)generation << 16);
}

static NetRestId rest_id_invalid(void) { return sentinel_u32; }
static bool      rest_id_valid(const NetRestId id) { return !sentinel_check(id); }

static NetRestRequest* rest_request_get(NetRest* rest, const NetRestId id) {
  const u16 index = rest_id_index(id);
  if (index >= rest->requestCount) {
    return null;
  }
  NetRestRequest* req = &rest->requests[index];
  if (req->generation != rest_id_generation(id)) {
    return null;
  }
  return req;
}

static NetRestId rest_request_acquire(NetRest* rest) {
  for (u16 i = 0; i != rest->requestCount; ++i) {
    NetRestRequest* req      = &rest->requests[i];
    i32             expected = NetRestState_Idle;
    if (thread_atomic_compare_exchange_i32((i32*)&req->state, &expected, NetRestState_Acquired)) {
      ++req->generation; // NOTE: Allowed to wrap.
      return rest_id_create(i, req->generation);
    }
  }
  return rest_id_invalid();
}

static void rest_worker_thread(void* data) {
  NetRest* rest = data;

  bool shutdown = false;
  while (!shutdown) {
    thread_mutex_lock(rest->workerMutex);
    {
      thread_cond_wait(rest->workerWakeCondition, rest->workerMutex);
      shutdown = rest->workerShutdown;
    }
    thread_mutex_unlock(rest->workerMutex);
  }
}

NetRest* net_rest_create(Allocator* alloc, u32 workerCount, u32 requestCount) {
  workerCount  = math_max(1, workerCount);
  requestCount = math_max(workerCount, requestCount);

  NetRest* rest = alloc_alloc_t(alloc, NetRest);

  *rest = (NetRest){
      .alloc = alloc,

      .workerMutex         = thread_mutex_create(g_allocHeap),
      .workerWakeCondition = thread_cond_create(g_allocHeap),
      .workerThreads       = alloc_array_t(alloc, ThreadHandle, workerCount),
      .workerCount         = workerCount,

      .requests     = alloc_array_t(alloc, NetRestRequest, requestCount),
      .requestCount = requestCount,
  };

  // Initialize requests.
  for (u32 i = 0; i != requestCount; ++i) {
    rest->requests[i] = (NetRestRequest){
        .buffer = dynstring_create(g_allocHeap, 0),
    };
  }

  // Spawn workers.
  for (u32 i = 0; i != workerCount; ++i) {
    const String         threadName = fmt_write_scratch("volo_rest_{}", fmt_int(i));
    const ThreadPriority threadPrio = ThreadPriority_Low;
    rest->workerThreads[i] = thread_start(rest_worker_thread, (void*)rest, threadName, threadPrio);
  }

  return rest;
}

void net_rest_destroy(NetRest* rest) {
  // Signal workers to shutdown.
  rest->workerShutdown = true;
  rest_wake_worker_all(rest);

  // Wait for workers to shutdown.
  for (u32 i = 0; i != rest->workerCount; ++i) {
    thread_join(rest->workerThreads[i]);
  }

  // Cleanup worker data.
  thread_mutex_destroy(rest->workerMutex);
  thread_mutex_destroy(rest->workerWakeCondition);
  alloc_free_array_t(rest->alloc, rest->workerThreads, rest->workerCount);

  // Cleanup requests.
  for (u32 i = 0; i != rest->requestCount; ++i) {
    NetRestRequest* req = &rest->requests[i];
    string_maybe_free(g_allocHeap, req->host);
    string_maybe_free(g_allocHeap, req->uri);
    net_http_auth_free(&req->auth, g_allocHeap);
    dynstring_destroy(&req->buffer);
  }
  alloc_free_array_t(rest->alloc, rest->requests, rest->requestCount);

  alloc_free_t(rest->alloc, rest);
}

NetRestId net_rest_get(
    NetRest*           rest,
    const String       host,
    const String       uri,
    const NetHttpAuth* auth,
    const NetHttpEtag* etag) {
  diag_assert(!string_is_empty(host));

  const NetRestId id = rest_request_acquire(rest);
  if (!rest_id_valid(id)) {
    return id; // No free request slots.
  }
  NetRestRequest* req = rest_request_get(rest, id);
  diag_assert(req);

  req->host = string_maybe_dup(g_allocHeap, host);
  req->uri  = string_maybe_dup(g_allocHeap, uri);
  if (auth) {
    req->auth = net_http_auth_clone(auth, g_allocHeap);
  } else {
    req->auth = (NetHttpAuth){0};
  }
  if (etag) {
    req->etag = *etag;
  } else {
    req->etag = (NetHttpEtag){0};
  }

  thread_atomic_store_i32((i32*)&req->state, NetRestState_Ready);
  rest_wake_worker_single(rest);

  return id;
}

bool net_rest_done(NetRest* rest, const NetRestId id) {
  NetRestRequest* req = rest_request_get(rest, id);
  if (!req) {
    return true;
  }
  return thread_atomic_load_i32((i32*)&req->state) == NetRestState_Finished;
}

NetResult net_rest_result(NetRest* rest, const NetRestId id) {
  NetRestRequest* req = rest_request_get(rest, id);
  if (!req) {
    return NetResult_RestIdInvalid;
  }
  if (thread_atomic_load_i32((i32*)&req->state) != NetRestState_Finished) {
    return NetResult_RestBusy;
  }
  return req->result;
}

String net_rest_data(NetRest* rest, const NetRestId id) {
  NetRestRequest* req = rest_request_get(rest, id);
  if (!req) {
    return string_empty;
  }
  if (thread_atomic_load_i32((i32*)&req->state) != NetRestState_Finished) {
    return string_empty;
  }
  return dynstring_view(&req->buffer);
}

const NetHttpEtag* net_rest_etag(NetRest* rest, const NetRestId id) {
  NetRestRequest* req = rest_request_get(rest, id);
  if (!req) {
    return null;
  }
  if (thread_atomic_load_i32((i32*)&req->state) != NetRestState_Finished) {
    return null;
  }
  return &req->etag;
}

bool net_rest_release(NetRest* rest, const NetRestId id) {
  NetRestRequest* req = rest_request_get(rest, id);
  if (!req) {
    return false;
  }
  if (thread_atomic_load_i32((i32*)&req->state) != NetRestState_Finished) {
    return false; // TODO: Should we support aborting requests?
  }

  string_maybe_free(g_allocHeap, req->host);
  string_maybe_free(g_allocHeap, req->uri);
  net_http_auth_free(&req->auth, g_allocHeap);
  dynstring_clear(&req->buffer);

  // Mark it as available for acquiring.
  thread_atomic_store_i32((i32*)&req->state, NetRestState_Idle);

  return true;
}
