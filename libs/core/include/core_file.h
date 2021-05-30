#pragma once
#include "core_string.h"

/**
 * Handle to an open os file.
 */
typedef struct sFile File;

typedef enum {
  File_Success = 0,
  File_DiskFull,
  File_NotFound,
  File_NoAccess,
  File_Locked,
  File_TooManyOpenFiles,
  File_UnknownError,

  File_ResultCount,
} FileResult;

extern File* g_file_stdin;
extern File* g_file_stdout;
extern File* g_file_stderr;

/**
 * Return a textual result message for a given FileResult.
 */
String file_result_str(FileResult);

/**
 * Synchronously write a string to a file.
 */
FileResult file_write_sync(File*, String);
