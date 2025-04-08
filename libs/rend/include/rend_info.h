#pragma once
#include "core.h"
#include "core_string.h"

typedef struct sRendInfo RendInfo;

typedef struct sRendInfoEntry {
  struct sRendInfoEntry* next;
  String                 name, desc, value;
} RendInfoEntry;

RendInfo* rend_info_create(Allocator*, usize memCapacity);
void      rend_info_destroy(Allocator*, RendInfo*);
void      rend_info_reset(RendInfo*);

const RendInfoEntry* rend_info_begin(const RendInfo*);
bool                 rend_info_push(RendInfo*, String name, String desc, String value);
