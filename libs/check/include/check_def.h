#pragma once
#include "check_spec.h"

/**
 * TODO: Document
 */
typedef struct sCheckDef CheckDef;

/**
 * TODO: Document
 */
typedef void (*CheckSpecRoutine)(CheckSpecContext*);

/**
 * TODO: Document
 */
#define register_spec(_CTX_, _NAME_)                                                               \
  void spec_name(_NAME_)(CheckSpecContext*);                                                       \
  check_register_spec(_CTX_, string_lit(#_NAME_), &spec_name(_NAME_))

/**
 * TODO: Document
 */
CheckDef* check_create(Allocator*);

/**
 * TODO: Document
 */
void check_destroy(CheckDef*);

/**
 * TODO: Document
 */
void check_register_spec(CheckDef*, String name, CheckSpecRoutine);
