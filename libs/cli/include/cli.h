#pragma once
#include "core.h"

/**
 * Forward header for the cli library.
 */

typedef struct sCliApp         CliApp;
typedef struct sCliInvocation  CliInvocation;
typedef struct sCliParseErrors CliParseErrors;
typedef struct sCliParseValues CliParseValues;
typedef u16                    CliId;

typedef bool (*CliValidateFunc)(const String input);
