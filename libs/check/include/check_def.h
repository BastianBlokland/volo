#pragma once
#include "check_spec.h"

/**
 * Definition for a TestSuite.
 */
typedef struct sCheckDef CheckDef;

typedef void (*CheckSpecRoutine)(CheckSpecContext*);

/**
 * Register a specification to a 'CheckDef' TestSuite definition.
 * Define specifications using the 'spec(name)' macro.
 */
#define register_spec(_CTX_, _NAME_)                                                               \
  void spec_name(_NAME_)(CheckSpecContext*);                                                       \
  check_register_spec(_CTX_, string_lit(#_NAME_), &spec_name(_NAME_))

/**
 * Create a new (empty) TestSuite definition.
 * Destroy using 'check_destroy()'.
 */
CheckDef* check_create(Allocator*);

/**
 * Destroy a TestSuite definition.
 */
void check_destroy(CheckDef*);

void check_register_spec(CheckDef*, String name, CheckSpecRoutine);
