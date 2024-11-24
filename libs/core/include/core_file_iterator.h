#pragma once
#include "core_file.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * File Iterator.
 */
typedef struct sFileIterator FileIterator;

/**
 * Found file entry.
 */
typedef struct {
  FileType type;
  String   name; // NOTE: String is allocated in scratch memory, should NOT be stored.
} FileIteratorEntry;

/**
 * FileIterator result-code.
 */
typedef enum {
  FileIteratorResult_Found = 0,
  FileIteratorResult_End,
  FileIteratorResult_NoAccess,
  FileIteratorResult_DirectoryDoesNotExist,
  FileIteratorResult_PathIsNotADirectory,
  FileIteratorResult_TooManyOpenFiles,
  FileIteratorResult_UnknownError,

  FileIteratorResult_Count,
} FileIteratorResult;

/**
 * Return a textual representation of the given FileIteratorResult.
 */
String file_iterator_result_str(FileIteratorResult);

/**
 * Create a new file-iterator.
 * Destroy using 'file_iterator_destroy()'.
 */
FileIterator* file_iterator_create(Allocator*, String path);

/**
 * Destroy a file-iterator.
 */
void file_iterator_destroy(FileIterator*);

/**
 * Retrieve the next entry from the file-iterator.
 * NOTE: Invalidates 'FileIteratorEntry' results from previous calls to this function.
 * NOTE: Order is non deterministic.
 *
 * Returns:
 * 'Found': An entry was found and written to the out pointer.
 * 'End':   End of the iterator has been reached; no more files left (nothing written to out ptr).
 * *:       An error has ocurred (nothing written to out ptr)
 */
FileIteratorResult file_iterator_next(FileIterator*, FileIteratorEntry* out);
