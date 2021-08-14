#pragma once
#include "core_dynarray.h"

#include "cli_app.h"
#include "core_types.h"

typedef enum {
  CliOptionType_Flag,
  CliOptionType_Arg,
} CliOptionType;

typedef struct {
  u8     shortName;
  String longName;
} CliFlag;

typedef struct {
  u16 position;
} CliArg;

typedef struct {
  CliOptionType  type;
  String         desc;
  CliOptionFlags flags;
  union {
    CliFlag dataFlag;
    CliArg  dataArg;
  };
} CliOption;

struct sCliApp {
  String     desc;
  DynArray   options; // CliOption[]
  Allocator* alloc;
};
