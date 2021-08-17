#pragma once
#include "core_alloc.h"
#include "core_string.h"

// Forward declare from 'cli_validate.h'.
typedef bool (*CliValidateFunc)(const String input);

typedef u16 CliId;

typedef enum {
  CliOptionFlags_None = 0,
  /**
   * Indicates that a option takes a value.
   */
  CliOptionFlags_Value = 1 << 0,
  /**
   * Indicates that a option takes one or more values.
   */
  CliOptionFlags_MultiValue = (1 << 1) | CliOptionFlags_Value,
  /**
   * Indicates that a option is required to be provided.
   */
  CliOptionFlags_Required = (1 << 2) | CliOptionFlags_Value,
} CliOptionFlags;

/**
 * Command-Line-Interface application.
 *
 * Describes an application including all the flags and arguments it provides.
 */
typedef struct sCliApp CliApp;

/**
 * Create a new CliApp.
 * Note: 'string_empty' can be passed as the desc if no description is available.
 *
 * Destroy using 'cli_app_destroy()'.
 */
CliApp* cli_app_create(Allocator*, String desc);

/**
 * Destroy a CliApp definition.
 */
void cli_app_destroy(CliApp*);

/**
 * Register a new flag to the given application.
 * Note: 0 can be passed as the 'character' to indicate that this flag has no short-form.
 *
 * Flags can be passed with both a short and a long form:
 * '-[character]'
 * '--[name]'
 *
 * Pre-condition: no other flag uses the same short-form 'character'.
 * Pre-condition: no other flag uses the same long-form 'name'.
 * Pre-condition: ascii_is_printable(character) || character == 0.
 * Pre-condition: name.size > 0.
 * Pre-condition: name.size <= 64.
 */
CliId cli_register_flag(CliApp*, u8 character, String name, CliOptionFlags);

/**
 * Register a new argument to the given application.
 * Position of arguments are derived from the order in which they are registered.
 *
 * Note: name is only used for display purposes and has no effect on parsing.
 * Note: name does not need to be unique.
 *
 * Pre-condition: name.size > 0.
 * Pre-condition: name.size <= 64.
 */
CliId cli_register_arg(CliApp*, String name, CliOptionFlags);

/**
 * Register a validation function for the given option.
 *
 * Validation functions are run during parsing and can be used to reject invalid input.
 *
 * Pre-condition: CliId is a valid option registered to the given application.
 * Pre-condition: Option takes a value.
 * Pre-condition: Option doesn't have a validator registered yet.
 * Pre-condition: CliValidateFunc != null.
 */
void cli_register_validator(CliApp*, CliId, CliValidateFunc);

/**
 * Indicates that two options cannot be used together.

 * Pre-condition: CliId's are valid options registered to the given application.
 * Pre-condition: No exclusion has been registered yet between the same options.
 */
void cli_register_exclusion(CliApp*, CliId a, CliId b);

/**
 * Add a description to a registered option.
 *
 * Pre-condition: CliId is a valid option registered to the given application.
 * Pre-condition: There is no description registered yet for this option.
 * Pre-condition: desc.size > 0.
 */
void cli_register_desc(CliApp*, CliId, String desc);
