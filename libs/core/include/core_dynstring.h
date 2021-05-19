#pragma once
#include "core_dynarray.h"
#include "core_string.h"

typedef DynArray DynString;

DynString dynstring_create(Allocator*, usize capacity);
void      dynstring_destroy(DynString*);
String    dynstring_view(const DynString*);
void      dynstring_append(DynString*, String);
