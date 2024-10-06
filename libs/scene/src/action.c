#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "scene_action.h"

typedef u8 ActionTypeStorage;

/**
 * TODO: Document queue layout.
 */
ecs_comp_define(SceneActionQueueComp) {
  void* data;
  u32   count, cap;
};

static usize action_queue_mem_size(const u32 cap) {
  return sizeof(SceneAction) * cap + sizeof(ActionTypeStorage) * cap;
}

static Mem action_queue_types(void* memPtr, const u32 cap) {
  const usize offset = sizeof(SceneAction) * cap;
  return mem_create(bits_ptr_offset(memPtr, offset), sizeof(ActionTypeStorage) * cap);
}

static Mem action_queue_defs(void* memPtr, const u32 cap) {
  return mem_create(memPtr, sizeof(SceneAction) * cap);
}

static ActionTypeStorage* action_entry_type(void* data, const u32 cap, const u32 index) {
  return (ActionTypeStorage*)action_queue_types(data, cap).ptr + index;
}

static SceneAction* action_entry_def(void* data, const u32 capacity, const u32 index) {
  return (SceneAction*)action_queue_defs(data, capacity).ptr + index;
}

static void ecs_destruct_action_queue(void* comp) {
  SceneActionQueueComp* q = comp;
  if (q->cap) {
    alloc_free(g_allocHeap, mem_create(q->data, action_queue_mem_size(q->cap)));
  }
}

NO_INLINE_HINT static void action_queue_grow(SceneActionQueueComp* q) {
  const u32   newCap     = bits_nextpow2(q->cap + 1);
  const usize newMemSize = action_queue_mem_size(newCap);
  void*       newData    = alloc_alloc(g_allocHeap, newMemSize, alignof(SceneAction)).ptr;
  diag_assert_msg(newData, "Allocation failed");

  if (q->cap) {
    // Copy the action types and definitions to the new allocation.
    mem_cpy(action_queue_types(newData, newCap), action_queue_types(q->data, q->cap));
    mem_cpy(action_queue_defs(newData, newCap), action_queue_defs(q->data, q->cap));

    // Free the old allocation.
    alloc_free(g_allocHeap, mem_create(q->data, action_queue_mem_size(q->cap)));
  }

  q->data = newData;
  q->cap  = newCap;
}

ecs_module_init(scene_action_module) {
  ecs_register_comp(SceneActionQueueComp, .destructor = ecs_destruct_action_queue);
}

SceneAction* scene_action_push(SceneActionQueueComp* q, const SceneActionType type) {
  if (q->count == q->cap) {
    action_queue_grow(q);
  }

  ActionTypeStorage* entryType = action_entry_type(q->data, q->cap, q->count);
  SceneAction*       entryDef  = action_entry_def(q->data, q->cap, q->count);
  ++q->count;

  *entryType = (ActionTypeStorage)type;
  return entryDef;
}
