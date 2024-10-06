#include "core_alloc.h"
#include "core_bits.h"
#include "scene_action.h"

typedef u8 ActionTypeStorage;

/**
 * TODO: Document queue layout.
 */
ecs_comp_define(SceneActionQueueComp) {
  void* memPtr;
  u32   count, capacity;
};

static usize action_queue_mem_size(const u32 capacity) {
  return sizeof(SceneAction) * capacity + sizeof(ActionTypeStorage) * capacity;
}

static ActionTypeStorage* action_entry_types(void* memPtr, const u32 capacity) {
  return bits_ptr_offset(memPtr, sizeof(SceneAction) * capacity);
}

static SceneAction* action_entry_defs(void* memPtr, const u32 capacity) {
  (void)capacity;
  return memPtr;
}

static ActionTypeStorage* action_entry_type(void* memPtr, const u32 capacity, const u32 index) {
  return action_entry_types(memPtr, capacity) + index;
}

static SceneAction* action_entry_def(void* memPtr, const u32 capacity, const u32 index) {
  return action_entry_defs(memPtr, capacity) + index;
}

static void ecs_destruct_action_queue(void* data) {
  SceneActionQueueComp* q = data;
  if (q->capacity) {
    alloc_free(g_allocHeap, mem_create(q->memPtr, action_queue_mem_size(q->capacity)));
  }
}

NO_INLINE_HINT void action_queue_grow(SceneActionQueueComp* q) {
  const u32   newCapacity = bits_nextpow2(q->capacity + 1);
  const usize newMemSize  = action_queue_mem_size(newCapacity);
  void*       newMemPtr   = alloc_alloc(g_allocHeap, newMemSize, alignof(SceneAction)).ptr;

  // Copy the data to the new allocation.
  for (u32 i = 0; i != q->count; ++i) {
    const ActionTypeStorage type = action_entry_type(q->memPtr, q->capacity, i);
    const SceneAction*      data = action_entry_def(q->memPtr, q->capacity, i);
  }

  // Free the old allocation.
  if (q->capacity) {
    alloc_free(g_allocHeap, mem_create(q->memPtr, action_queue_mem_size(q->capacity)));
  }

  q->memPtr   = newMemPtr;
  q->capacity = newCapacity;
}

ecs_module_init(scene_action_module) {
  ecs_register_comp(SceneActionQueueComp, .destructor = ecs_destruct_action_queue);
}

SceneAction* scene_action_push(SceneActionQueueComp* q, const SceneActionType type) {
  if (q->count == q->capacity) {
    action_queue_grow(q);
  }

  ActionTypeStorage* entryType = action_entry_type(q->memPtr, q->capacity, q->count);
  SceneAction*       entryDef  = action_entry_def(q->memPtr, q->capacity, q->count);
  ++q->count;

  *entryType = (ActionTypeStorage)type;
  return entryDef;
}
