#pragma once
#include "core_dynarray.h"

#include "cli_app.h"

typedef enum {
  CliOptionType_Flag,
  CliOptionType_Arg,
} CliOptionType;

typedef struct {
  u8     character;
  String name;
} CliFlag;

typedef struct {
  u16    position;
  String name;
} CliArg;

typedef struct {
  CliOptionType  type;
  CliOptionFlags flags;
  union {
    CliFlag dataFlag;
    CliArg  dataArg;
  };
  CliValidateFunc validator;
} CliOption;

struct sCliApp {
  String     desc;
  DynArray   options; // CliOption[]
  Allocator* alloc;
};

CliOption* cli_option(const CliApp*, CliId);
String     cli_option_name(const CliApp* app, CliId);

/**
 * Find an option by its character
 * Note: Returns 'sentinel_u16' if no option was found with the given character
 */
CliId cli_find_by_character(const CliApp*, u8 character);

/**
 * Find an option by its name.
 * Note: Returns 'sentinel_u16' if no option was found with the given name.
 */
CliId cli_find_by_name(const CliApp*, String name);

/**
 * Find an option by its position.
 * Note: Returns 'sentinel_u16' if no option was found at the given position.
 */
CliId cli_find_by_position(const CliApp*, u16 position);
