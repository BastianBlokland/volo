#pragma once
#include "core_array.h"
#include "core_macro.h"
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

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
  /**
   * Indicates that a option takes one or more values and is required to be provided.
   */
  CliOptionFlags_RequiredMultiValue = CliOptionFlags_MultiValue | CliOptionFlags_Required,
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
 * Pre-condition: a != b.
 */
void cli_register_exclusion(CliApp*, CliId a, CliId b);

/**
 * Indicates that an option cannot be used together with any of the given other options.

 * Pre-condition: CliId's are valid options registered to the given application.
 * Pre-condition: No exclusion has been registered yet between the same options.
 */
#define cli_register_exclusions(_CLI_APP_, _CLI_ID_, ...)                                          \
  cli_register_exclusions_raw(                                                                     \
      (_CLI_APP_), (_CLI_ID_), (const CliId[]){__VA_ARGS__}, COUNT_VA_ARGS(__VA_ARGS__))

void cli_register_exclusions_raw(CliApp*, CliId id, const CliId* otherIds, usize otherCount);

/**
 * Add a description to a registered option.
 *
 * Pre-condition: CliId is a valid option registered to the given application.
 * Pre-condition: There is no description registered yet for this option.
 * Pre-condition: desc.size > 0.
 */
void cli_register_desc(CliApp*, CliId, String desc);

/**
 * Add a description including the possible choices to a registered option.
 * Note: provide 'sentinel_usize' to 'defaultChoice' to indicate that there is no default choice.
 *
 * Pre-condition: CliId is a valid option registered to the given application.
 * Pre-condition: There is no description registered yet for this option.
 * Pre-condition: choiceCount <= 1024.
 * Pre-condition: defaultChoice < choiceCount || sentinel_check(defaultChoice).
 * Pre-condition: Formatted description fits in 1KiB.
 */
#define cli_register_desc_choice_array(                                                            \
    _CLI_APP_, _CLI_ID_, _DESC_, _CHOICES_ARRAY_, _DEFAULT_CHOICE_)                                \
  cli_register_desc_choice(                                                                        \
      (_CLI_APP_),                                                                                 \
      (_CLI_ID_),                                                                                  \
      (_DESC_),                                                                                    \
      (_CHOICES_ARRAY_),                                                                           \
      array_elems(_CHOICES_ARRAY_),                                                                \
      (_DEFAULT_CHOICE_))

/**
 * Add a description including the possible choices to a registered option.
 * Note: provide 'sentinel_usize' to 'defaultChoice' to indicate that there is no default choice.
 *
 * Pre-condition: CliId is a valid option registered to the given application.
 * Pre-condition: There is no description registered yet for this option.
 * Pre-condition: choiceCount <= 1024.
 * Pre-condition: defaultChoice < choiceCount || sentinel_check(defaultChoice).
 * Pre-condition: Formatted description fits in 1KiB.
 */
void cli_register_desc_choice(
    CliApp*, CliId, String desc, const String* choiceStrs, usize choiceCount, usize defaultChoice);

/**
 * Retrieve the description for the given option.
 * Returns 'string_empty' when no description was registered for the given option.
 *
 * Pre-condition: CliId is a valid option registered to the given application.
 */
String cli_desc(const CliApp*, CliId);

/**
 * Check if either option excludes the other.
 *
 * Pre-condition: a is a valid option registered to the given application.
 * Pre-condition: b is a valid option registered to the given application.
 */
bool cli_excludes(const CliApp*, CliId a, CliId b);
