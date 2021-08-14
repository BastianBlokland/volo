#pragma once
#include "core_alloc.h"
#include "core_string.h"

typedef u16 CliId;

typedef enum {
  CliOptionFlags_None       = 0,
  CliOptionFlags_Value      = 1 << 0,
  CliOptionFlags_MultiValue = (1 << 1) | CliOptionFlags_Value,
  CliOptionFlags_Required   = (1 << 2) | CliOptionFlags_Value,
} CliOptionFlags;

/**
 * TODO.
 */
typedef struct sCliApp CliApp;

/**
 * TODO.
 */
CliApp* cli_app_create(Allocator*, String desc);

/**
 * TODO.
 */
void cli_app_destroy(CliApp*);

/**
 * TODO.
 */
CliId cli_register_flag(CliApp*, u8 character, String name, CliOptionFlags);

/**
 * TODO.
 */
CliId cli_register_arg(CliApp*, String name, CliOptionFlags);
