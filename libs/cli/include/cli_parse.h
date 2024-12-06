#pragma once
#include "cli.h"
#include "core_string.h"

typedef enum {
  CliParseResult_Success = 0,
  CliParseResult_Fail    = 1,
} CliParseResult;

typedef struct sCliParseErrors {
  String* values;
  usize   count;
} CliParseErrors;

typedef struct sCliParseValues {
  String* values;
  usize   count;
} CliParseValues;

/**
 * Command-Line-Interface invocation.
 *
 * Contains the result of parsing a set of input strings for a specific application.
 */
typedef struct sCliInvocation CliInvocation;

/**
 * Parse a set of input strings for the given application.
 *
 * NOTE: Does not strip off the initial invocation-path that (many?) operating systems pass as the
 * first argument.
 *
 * Destroy using 'cli_parse_destroy()'.
 */
CliInvocation* cli_parse(const CliApp*, int argc, const char** argv);

/**
 * Destroy a CliInvocation.
 */
void cli_parse_destroy(CliInvocation*);

/**
 * Retrieve the result of parsing the given invocation.
 */
CliParseResult cli_parse_result(const CliInvocation*);

/**
 * Retrieve the errors that occurred during the parsing of the given invocation.
 */
CliParseErrors cli_parse_errors(const CliInvocation*);

/**
 * Check if a specific option is provided in the given invocation.
 */
bool cli_parse_provided(const CliInvocation*, CliId);

/**
 * Retrieve the values that where provided to the specific option in this invocation.
 * NOTE: Returns an empty set of values if the given option was not provided.
 */
CliParseValues cli_parse_values(const CliInvocation*, CliId);
