#include "core_alloc.h"
#include "geo_query.h"

struct sQueryEnv {
  Allocator* alloc;
};

QueryEnv* geo_query_env_create(Allocator* alloc) {
  QueryEnv* env = alloc_alloc_t(alloc, QueryEnv);

  *env = (QueryEnv){
      .alloc = alloc,
  };
  return env;
}

void geo_query_env_destroy(QueryEnv* env) { alloc_free_t(env->alloc, env); }
