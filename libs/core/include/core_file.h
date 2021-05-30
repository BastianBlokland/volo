#pragma once
#include "core_dynstring.h"
#include "core_string.h"

/**
 * Handle to an open os file.
 */
typedef struct sFile File;

typedef enum : u16 {
  File_Success = 0,
  File_NoDataAvailable,
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

/**
 * Synchronously read a block of available data in the dynamic-string.
 * Note: returns 'File_NoDataAvailable' when the end of the file has been reached.
 */
FileResult file_read_sync(File*, DynString*);
