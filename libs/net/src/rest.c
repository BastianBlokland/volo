#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_format.h"
#include "core_math.h"
#include "core_thread.h"
#include "core_time.h"
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

typedef enum {
  NetRestType_Head, // Http HEAD request.
  NetRestType_Get,  // Http GET request.
} NetRestType;

typedef struct {
  NetRestState state;
  NetRestType  type;
  NetResult    result;
  u16          generation;
  String       host, uri;
  NetHttpAuth  auth;
  NetHttpEtag  etag;
  DynString    buffer;
} NetRestRequest;

typedef struct sNetRest {
  Allocator*   alloc;
  NetHttpFlags httpFlags;

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

static NetRestState rest_request_state_load(NetRestRequest* req) {
  return (NetRestState)thread_atomic_load_i32((i32*)&req->state);
}

static void rest_request_state_store(NetRestRequest* req, const NetRestState state) {
  thread_atomic_store_i32((i32*)&req->state, state);
}

static bool rest_request_state_transition(NetRestRequest* req, NetRestState from, NetRestState to) {
  return thread_atomic_compare_exchange_i32((i32*)&req->state, (i32*)&from, to);
}

static NetRestId rest_request_acquire(NetRest* rest) {
  for (u16 i = 0; i != rest->requestCount; ++i) {
    NetRestRequest* req = &rest->requests[i];
    if (rest_request_state_transition(req, NetRestState_Idle, NetRestState_Acquired)) {
      ++req->generation; // NOTE: Allowed to wrap.
      return rest_id_create(i, req->generation);
    }
  }
  return rest_id_invalid();
}

static NetRestRequest* rest_worker_take_any(NetRest* rest) {
  for (u16 i = 0; i != rest->requestCount; ++i) {
    NetRestRequest* req = &rest->requests[i];
    if (rest_request_state_transition(req, NetRestState_Ready, NetRestState_Busy)) {
      return req;
    }
  }
  return null; // No work found.
}

static NetRestRequest* rest_worker_take_for_host(NetRest* rest, const String host) {
  for (u16 i = 0; i != rest->requestCount; ++i) {
    NetRestRequest* req = &rest->requests[i];
    if (rest_request_state_transition(req, NetRestState_Ready, NetRestState_Busy)) {
      if (string_eq(req->host, host)) {
        return req;
      }
      rest_request_state_store(req, NetRestState_Ready);
    }
  }
  return null; // No work found.
}

static bool rest_worker_should_retry(const NetResult result) {
  switch (result) {
  // Valid results.
  case NetResult_Success:
  case NetResult_HttpNotModified:
  case NetResult_HttpNotFound:
  case NetResult_HttpUnauthorized:
  case NetResult_HttpForbidden:
  case NetResult_HttpRedirected:
    return false;

  // Unsupported features.
  case NetResult_Unsupported:
  case NetResult_HttpUnsupportedProtocol:
  case NetResult_HttpUnsupportedVersion:
  case NetResult_HttpUnsupportedTransferEncoding:
  case NetResult_HttpUnsupportedContentEncoding:
    return false;

    // Unrecoverable system errors.
  case NetResult_SystemFailure:
  case NetResult_TlsUnavailable:
    return false;

  default:
    return true; // Maybe retried.
  }
}

static void rest_worker_thread(void* data) {
  NetRest* rest = data;

  enum { MaxTries = 3 };
  const TimeDuration retrySleep[MaxTries] = {
      [1] = time_milliseconds(500),
      [2] = time_second,
  };

  NetHttp*     con            = null;
  TimeDuration conLastReqTime = 0;

  while (!rest->workerShutdown) {
    NetRestRequest* req         = null;
    u32             reqTryIndex = 0;
    if (con) {
      req = rest_worker_take_for_host(rest, net_http_remote_name(con));
    }
    if (!req) {
      req = rest_worker_take_any(rest);
    }
    if (!req) {
      goto Sleep;
    }

  Retry:
    if (con && net_http_status(con) != NetResult_Success) {
      net_http_shutdown_sync(con);
      net_http_destroy(con);
      con = null;
    }
    if (con && !string_eq(net_http_remote_name(con), req->host)) {
      net_http_shutdown_sync(con);
      net_http_destroy(con);
      con = null;
    }
    if (retrySleep[reqTryIndex]) {
      // TODO: Instead of sleeping the worker we should put the request back and process it after
      // the retry time has expired. That way other requests are not blocked.
      thread_sleep(retrySleep[reqTryIndex]);
    }
    if (!con) {
      con = net_http_connect_sync(g_allocHeap, req->host, rest->httpFlags);
    }
    conLastReqTime = time_steady_clock();

    switch (req->type) {
    case NetRestType_Head:
      req->result = net_http_head_sync(con, req->uri, &req->auth, &req->etag);
      break;
    case NetRestType_Get:
      req->result = net_http_get_sync(con, req->uri, &req->auth, &req->etag, &req->buffer);
      break;
    }

    if (rest_worker_should_retry(req->result) && reqTryIndex < MaxTries) {
      ++reqTryIndex;
      goto Retry;
    }
    rest_request_state_store(req, NetRestState_Finished);
    continue; // Process the next request.

  Sleep:
    if (con && time_steady_duration(conLastReqTime, time_steady_clock()) > time_seconds(30)) {
      // Connection has been inactive for a while; close it.
      net_http_shutdown_sync(con);
      net_http_destroy(con);
      con = null;
    }
    thread_mutex_lock(rest->workerMutex);
    if (con) {
      thread_cond_wait_timeout(rest->workerWakeCondition, rest->workerMutex, time_seconds(10));
    } else {
      thread_cond_wait(rest->workerWakeCondition, rest->workerMutex);
    }
    thread_mutex_unlock(rest->workerMutex);
  }

  // Shutdown.
  if (con) {
    net_http_shutdown_sync(con);
    net_http_destroy(con);
  }
}

NetRest*
net_rest_create(Allocator* alloc, u32 workerCount, u32 requestCount, const NetHttpFlags httpFlags) {
  workerCount  = math_max(1, workerCount);
  requestCount = math_max(workerCount, requestCount);

  NetRest* rest = alloc_alloc_t(alloc, NetRest);

  *rest = (NetRest){
      .alloc     = alloc,
      .httpFlags = httpFlags,

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

NetRestId net_rest_head(
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

  req->type = NetRestType_Head;
  req->host = string_maybe_dup(g_allocHeap, host);
  req->uri  = string_maybe_dup(g_allocHeap, uri);
  req->auth = auth ? net_http_auth_clone(auth, g_allocHeap) : (NetHttpAuth){0};
  req->etag = etag ? *etag : (NetHttpEtag){0};

  rest_request_state_store(req, NetRestState_Ready);
  rest_wake_worker_single(rest);

  return id;
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

  req->type = NetRestType_Get;
  req->host = string_maybe_dup(g_allocHeap, host);
  req->uri  = string_maybe_dup(g_allocHeap, uri);
  req->auth = auth ? net_http_auth_clone(auth, g_allocHeap) : (NetHttpAuth){0};
  req->etag = etag ? *etag : (NetHttpEtag){0};

  rest_request_state_store(req, NetRestState_Ready);
  rest_wake_worker_single(rest);

  return id;
}

bool net_rest_done(NetRest* rest, const NetRestId id) {
  NetRestRequest* req = rest_request_get(rest, id);
  if (!req) {
    return true;
  }
  return rest_request_state_load(req) == NetRestState_Finished;
}

NetResult net_rest_result(NetRest* rest, const NetRestId id) {
  NetRestRequest* req = rest_request_get(rest, id);
  if (!req) {
    return NetResult_RestIdInvalid;
  }
  if (rest_request_state_load(req) != NetRestState_Finished) {
    return NetResult_RestBusy;
  }
  return req->result;
}

String net_rest_data(NetRest* rest, const NetRestId id) {
  NetRestRequest* req = rest_request_get(rest, id);
  if (!req) {
    return string_empty;
  }
  if (rest_request_state_load(req) != NetRestState_Finished) {
    return string_empty;
  }
  return dynstring_view(&req->buffer);
}

const NetHttpEtag* net_rest_etag(NetRest* rest, const NetRestId id) {
  NetRestRequest* req = rest_request_get(rest, id);
  if (!req) {
    return null;
  }
  if (rest_request_state_load(req) != NetRestState_Finished) {
    return null;
  }
  return &req->etag;
}

bool net_rest_release(NetRest* rest, const NetRestId id) {
  NetRestRequest* req = rest_request_get(rest, id);
  if (!req) {
    return false;
  }
  if (rest_request_state_load(req) != NetRestState_Finished) {
    return false; // TODO: Support aborting requests.
  }

  // Free resources.
  string_maybe_free(g_allocHeap, req->host);
  string_maybe_free(g_allocHeap, req->uri);
  net_http_auth_free(&req->auth, g_allocHeap);
  dynstring_clear(&req->buffer);

  // Cleanup (not needed for correctness but makes debugging easier).
  req->result = NetResult_Success;
  req->host   = string_empty;
  req->uri    = string_empty;
  req->auth   = (NetHttpAuth){0};
  req->etag   = (NetHttpEtag){0};

  // Mark the request as available for reuse.
  rest_request_state_store(req, NetRestState_Idle);
  return true;
}
