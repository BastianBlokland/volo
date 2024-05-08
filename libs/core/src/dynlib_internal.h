#pragma once
#include "core_dynlib.h"

void         dynlib_pal_init(void);
DynLibResult dynlib_pal_load(Allocator*, String name, DynLib** out);
void         dynlib_pal_destroy(DynLib*);
String       dynlib_pal_path(const DynLib*);
Symbol       dynlib_pal_symbol(const DynLib*, String name);
