#pragma once
#include "core.h"
#include "core_string.h"

typedef struct sRendInfo      RendInfo;
typedef struct sRendInfoEntry RendInfoEntry;

RendInfo* rend_info_create(Allocator*, usize memCapacity);
void      rend_info_destroy(Allocator*, RendInfo*);
void      rend_info_reset(RendInfo*);

const RendInfoEntry* rend_info_begin(const RendInfo*);
const RendInfoEntry* rend_info_next(const RendInfo*, const RendInfoEntry*);

String rend_info_name(const RendInfo*, const RendInfoEntry*);
String rend_info_desc(const RendInfo*, const RendInfoEntry*);
String rend_info_value(const RendInfo*, const RendInfoEntry*);

bool rend_info_push(RendInfo*, String name, String desc, String value);
