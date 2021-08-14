#pragma once
#include "core_string.h"
#include "core_types.h"

// Forward declare from 'cli_app.h'.
typedef struct sCliApp CliApp;

typedef u16 CliId;

typedef enum {
  CliParseResult_Success = 0,
  CliParseResult_Fail    = 1,
} CliParseResult;

typedef struct {
  String* head;
  usize   count;
} CliParseErrors;

typedef struct {
  String* head;
  usize   count;
} CliParseValues;

/**
 * TODO.
 */
typedef struct sCliInvocation CliInvocation;

/**
 * TODO.
 */
CliInvocation* cli_parse(const CliApp*, int argc, const char** argv);

/**
 * TODO.
 */
void cli_parse_destroy(CliInvocation*);

/**
 * TODO.
 */
CliParseResult cli_parse_result(CliInvocation*);

/**
 * TODO.
 */
CliParseErrors cli_parse_errors(CliInvocation*);

/**
 * TODO.
 */
bool cli_parse_provided(CliInvocation*, CliId);

/**
 * TODO.
 */
CliParseValues cli_parse_values(CliInvocation*, CliId);
