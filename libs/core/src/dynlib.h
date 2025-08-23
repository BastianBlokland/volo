#pragma once
#include "core/dynlib.h"

void         dynlib_pal_init(void);
void         dynlib_pal_teardown(void);
DynLibResult dynlib_pal_load(Allocator*, String name, DynLib** out);
void         dynlib_pal_destroy(DynLib*);
String       dynlib_pal_path(const DynLib*);
Symbol       dynlib_pal_symbol(const DynLib*, String name);
Symbol       dynlib_pal_symbol_global(String name);
