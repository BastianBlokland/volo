#include "core_alloc.h"
#include "core_process.h"

struct sProcess {
  Allocator*   alloc;
  ProcessFlags flags;
};

Process* process_create(Allocator* alloc, const ProcessFlags flags) {
  Process* itr = alloc_alloc_t(alloc, Process);

  *itr = (Process){
      .alloc = alloc,
      .flags = flags,
  };

  return itr;
}

void process_destroy(Process* itr) { alloc_free_t(itr->alloc, itr); }

// void      process_kill(Process*);
// i32       process_block(Process*);
// void      process_signal(Process*, Signal);
// ProcessId process_id(const Process*);
