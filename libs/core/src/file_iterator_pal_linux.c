#include "core_alloc.h"
#include "core_file_iterator.h"

struct sFileIterator {
  Allocator* alloc;
};

FileIterator* file_iterator_create(Allocator* alloc, const String directoryPath) {
  (void)directoryPath;

  FileIterator* itr = alloc_alloc_t(alloc, FileIterator);

  *itr = (FileIterator){
      .alloc = alloc,
  };

  return itr;
}

void file_iterator_destroy(FileIterator* itr) { alloc_free_t(itr->alloc, itr); }

FileIteratorResult file_iterator_next(FileIterator* itr, FileIteratorEntry* out) {
  (void)itr;
  (void)out;
  return FileIteratorResult_UnknownError;
}
