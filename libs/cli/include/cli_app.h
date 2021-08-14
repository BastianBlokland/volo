#pragma once
#include "core_alloc.h"
#include "core_string.h"

typedef u32 CliId;

typedef enum {
  CliOptionFlags_None           = 0,
  CliOptionFlags_Optional       = 1 << 0,
  CliOptionFlags_WithValue      = 1 << 1,
  CliOptionFlags_WithMultiValue = (1 << 2) | CliOptionFlags_WithValue,
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
CliId cli_register_flag(CliApp*, u8 shortName, String longName, String desc, CliOptionFlags);

/**
 * TODO.
 */
CliId cli_register_arg(CliApp*, String desc, CliOptionFlags);
