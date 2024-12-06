#pragma once
#include "core_dynstring.h"
#include "core_string.h"

/**
 * Handle to an open os file.
 */
typedef struct sFile File;

/**
 * File result-code.
 */
typedef enum eFileResult {
  FileResult_Success = 0,
  FileResult_AlreadyExists,
  FileResult_DiskFull,
  FileResult_InvalidFilename,
  FileResult_Locked,
  FileResult_NoAccess,
  FileResult_NoDataAvailable,
  FileResult_NotFound,
  FileResult_PathTooLong,
  FileResult_PathInvalid,
  FileResult_TooManyOpenFiles,
  FileResult_IsDirectory,
  FileResult_AllocationFailed,
  FileResult_FileEmpty,
  FileResult_UnknownError,

  FileResult_Count,
} FileResult;

/**
 * Mode to open a file with.
 * - Open: Open an existing file.
 *    > fails if the file does not exist.
 *    > Head is at the start.
 * - Append: Append to an existing file or create a new file.
 *    > Head is at the end.
 * - Create: Open an existing file or create a new file.
 *    > Head is at the start.
 */
typedef enum eFileMode {
  FileMode_Open,
  FileMode_Append,
  FileMode_Create,
} FileMode;

/**
 * Access to request when opening a file.
 */
typedef enum eFileAccessFlags {
  FileAccess_None  = 0,
  FileAccess_Read  = 1 << 0,
  FileAccess_Write = 1 << 1,
} FileAccessFlags;

/**
 * File Type code.
 */
typedef enum eFileType {
  FileType_None,
  FileType_Regular,
  FileType_Directory,
  FileType_Unknown,
} FileType;

/**
 * Output structure for 'file_stat_sync'.
 */
typedef struct sFileInfo {
  usize    size;
  FileType type;
  TimeReal accessTime, modTime;
} FileInfo;

/**
 * File mapping (performance) hints.
 */
typedef enum eFileHints {
  FileHints_None     = 0,
  FileHints_Prefetch = 1 << 0, // Start reading the file in the background.
} FileHints;

extern File* g_fileStdIn;
extern File* g_fileStdOut;
extern File* g_fileStdErr;

/**
 * Return a textual representation of the given FileResult.
 */
String file_result_str(FileResult);

/**
 * Create a file handle.
 * On success a file will be assigned to the file pointer. Retrieved file pointer should be
 * destroyed with 'file_destroy'.
 */
FileResult file_create(Allocator*, String path, FileMode, FileAccessFlags, File** file);

/**
 * Create a temporary file.
 * On success a file will be assigned to the file pointer. Retrieved file pointer should be
 * destroyed with 'file_destroy'.
 */
FileResult file_temp(Allocator*, File** file);

/**
 * Destroy a file handle.
 * NOTE: Does not destroy the file on the file-system, only closes the handle.
 */
void file_destroy(File*);

/**
 * Synchronously write a string to a file.
 */
FileResult file_write_sync(File*, String);

/**
 * Synchronously write a string to a new file at the given path.
 * NOTE: The atomic version writes to a temp file and then renames to the final path.
 */
FileResult file_write_to_path_sync(String path, String data);
FileResult file_write_to_path_atomic(String path, String data);

/**
 * Synchronously read a block of available data in the dynamic-string.
 * NOTE: returns 'FileResult_NoDataAvailable' when the end of the file has been reached.
 */
FileResult file_read_sync(File*, DynString*);

/**
 * Keep reading synchronously into the dynamic-string until the end of the file is reached.
 */
FileResult file_read_to_end_sync(File*, DynString*);

/**
 * Synchronously seek an open file to the specified position.
 */
FileResult file_seek_sync(File*, usize position);

/**
 * Synchronously retrieve information about a file.
 */
FileInfo file_stat_sync(File*);
FileInfo file_stat_path_sync(String path);

/**
 * Synchronously delete a file from the file-system.
 */
FileResult file_delete_sync(String path);

/**
 * Synchronously delete a directory from the file-system.
 */
FileResult file_delete_dir_sync(String path);

/**
 * Memory map the given file.
 * On success the mapped memory will be assigned to the output pointer. Memory mappings are
 * automatically closed when the file-handle is destroyed (or when calling 'file_unmap()').
 *
 * Pre-condition: file has not been mapped yet.
 */
FileResult file_map(File*, String* output, FileHints);

/**
 * Release the memory of the given file.
 *
 * Pre-condition: file has been mapped.
 */
FileResult file_unmap(File*);

/**
 * Rename the file at the given path.
 * NOTE: oldPath and newPath need to be on the same filesystem.
 */
FileResult file_rename(String oldPath, String newPath);

/**
 * Synchronously create a new file-system directory.
 * NOTE: Will also create the parent directory if its missing.
 */
FileResult file_create_dir_sync(String path);

/**
 * Global file statistics.
 */
u32   file_count(void);
usize file_mapping_size(void);
