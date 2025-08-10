#pragma once
#include "core.h"

/**
 * Version specification.
 */
typedef struct {
  u32 major, minor, patch;
  u8  label[52]; // Unused characters are zero filled.
} Version;

/**
 * Query the version of the running executable.
 */
Version version_executable(void);

/**
 * Lookup the version string for the given version.
 */
String version_label(const Version*);

/**
 * Compare versions.
 * NOTE: Labels are ignored for these check.
 */
bool version_equal(const Version* a, const Version* b);
bool version_newer(const Version* a, const Version* b);
bool version_compatible(const Version* a, const Version* b);

/**
 * Create a human readable string from the given version.
 * Format: Major.Minor.Patch+Label
 */
void   version_str(const Version*, DynString* out);
String version_str_scratch(const Version*);
