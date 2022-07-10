#pragma once

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Environment for querying against.
 */
typedef struct sQueryEnv QueryEnv;

/**
 * Create a new QueryEnv instance.
 * Destroy using 'geo_query_env_destroy()'.
 */
QueryEnv* geo_query_env_create(Allocator*);

/**
 * Destroy a QueryEnv instance.
 */
void geo_query_env_destroy(QueryEnv*);
