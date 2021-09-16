#pragma once
#include "core_array.h"
#include "core_string.h"

// Forward declare from 'cli_parse.h'.
typedef struct sCliInvocation CliInvocation;

// Forward declare from 'cli_app.h'.
typedef u16 CliId;

/**
 * Read a string that was provided to the given option.
 * If the option was not provided then 'defaultVal' is returned.
 */
String cli_read_string(const CliInvocation*, CliId, String defaultVal);

/**
 * Read a i64 integer that was provided to the given option.
 * If the option was not provided then 'defaultVal' is returned.
 */
i64 cli_read_i64(const CliInvocation*, CliId, i64 defaultVal);

/**
 * Read a u64 integer that was provided to the given option.
 * If the option was not provided then 'defaultVal' is returned.
 */
u64 cli_read_u64(const CliInvocation*, CliId, u64 defaultVal);

/**
 * Read a f64 floating point number that was provided to the given option.
 * If the option was not provided then 'defaultVal' is returned.
 */
f64 cli_read_f64(const CliInvocation*, CliId, f64 defaultVal);

/**
 * Read the index of the choice that was provided to the given option.
 * If the given input doesn't match any of the choice strings then 'defaultVal' is returned.
 * If the option was not provided then 'defaultVal' is returned.
 */
#define cli_read_choice_array(_CLI_INVOCATION_, _CLI_ID_, _CHOICES_ARRAY_, _DEFAULT_VAL_)          \
  cli_read_choice(                                                                                 \
      (_CLI_INVOCATION_),                                                                          \
      (_CLI_ID_),                                                                                  \
      (_CHOICES_ARRAY_),                                                                           \
      array_elems(_CHOICES_ARRAY_),                                                                \
      (_DEFAULT_VAL_))

/**
 * Read the index of the choice that was provided to the given option.
 * If the given input doesn't match any of the choice strings then 'defaultVal' is returned.
 * If the option was not provided then 'defaultVal' is returned.
 */
usize cli_read_choice(
    const CliInvocation*, CliId, const String* choiceStrs, usize choiceCount, usize defaultVal);
