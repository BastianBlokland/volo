#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "log_logger.h"

#include "buffer_internal.h"
#include "comp_internal.h"
#include "def_internal.h"

/**
 * Modifications are stored per entity. Entity data is kept sorted so a binary-search can be
 * performed to find the data. At the time of writing this seems like a reasonable space / time
 * tradeof, but in the future alternatives like hashed lookups could be explored.
 *
 * Component additions are currently stored in a chunked memory allocator with pointers to the next
 * added component (for that same entity) to form an intrusive linked-list.
 *
 * Added component memory layout:
 * - EcsCompId (2 bytes)
 * - EcsBufferCompData* (sizeof(void*) bytes)
 * - [PADDING] (padded to reach required component alignment)
 * - [RAW COMPONENT DATA]
 *
 * Reasons for storing it in a chunked allocator is to have components relatively close in memory
 * (as sequential component additions end up in the same chunk) while also being to return a stable
 * pointer to the caller (stable until the buffer is cleared).
 */

#define ecs_buffer_compdata_chunk_size (256 * usize_kibibyte)

typedef u32 EcsBufferMaskId;

struct sEcsBufferCompData {
  EcsCompId                  id;
  struct sEcsBufferCompData* next; // Next added component for the same entity.
};

typedef struct {
  EcsEntityId          id;
  EcsBufferEntityFlags flags;
  EcsBufferMaskId      addMask, removeMask;
  EcsBufferCompData*   compHead; // Head of the linked-list of added components.
} EcsBufferEntity;

static i8 ecs_buffer_compare_entity(const void* a, const void* b) {
  const u32 serialA = ecs_entity_id_serial(((const EcsBufferEntity*)a)->id);
  const u32 serialB = ecs_entity_id_serial(((const EcsBufferEntity*)b)->id);
  return serialA < serialB ? -1 : serialA > serialB ? 1 : 0;
}

/**
 * Store a new component-mask.
 * Because component-masks have a fixed size we can trivially look them up by index later.
 */
static EcsBufferMaskId ecs_buffer_mask_add(EcsBuffer* buffer) {
  const EcsBufferMaskId id = (EcsBufferMaskId)buffer->masks.size;
  mem_set(dynarray_push(&buffer->masks, 1), 0);
  return id;
}

/**
 * Retrieve a stored component-mask.
 * NOTE: The returned view is invalidated when new masks are registered or the buffer is cleared.
 */
static BitSet ecs_buffer_mask(EcsBuffer* buffer, const EcsBufferMaskId id) {
  return dynarray_at(&buffer->masks, id, 1);
}

static EcsBufferEntity* ecs_buffer_entity_get(EcsBuffer* buffer, const EcsEntityId id) {
  EcsBufferEntity* result = dynarray_find_or_insert_sorted(
      &buffer->entities, ecs_buffer_compare_entity, mem_struct(EcsBufferEntity, .id = id).ptr);

  if (!result->id) {
    result->id         = id;
    result->addMask    = ecs_buffer_mask_add(buffer);
    result->removeMask = ecs_buffer_mask_add(buffer);
  }
  return result;
}

/**
 * Calculate the required alignment for the 'EcsBufferCompData' header + component payload.
 */
static usize ecs_buffer_compdata_align(usize compAlign) {
  return math_max(alignof(EcsBufferCompData), compAlign);
}

/**
 * Calculate the padding between the 'EcsBufferCompData' header and the component payload.
 */
static usize ecs_buffer_compdata_padding(usize compAlign) {
  if (compAlign > sizeof(EcsBufferCompData)) {
    return compAlign - sizeof(EcsBufferCompData);
  }
  return 0;
}

static Mem ecs_buffer_compdata_payload(
    const EcsBufferCompData* data, const usize compSize, const usize compAlign) {

  const usize padding = ecs_buffer_compdata_padding(compAlign);
  void*       res     = bits_ptr_offset(data, sizeof(EcsBufferCompData) + padding);

  diag_assert(bits_aligned_ptr(res, compAlign));
  return mem_create(res, compSize);
}

static EcsBufferCompData* ecs_buffer_compdata_add(
    EcsBuffer* buffer, const EcsCompId compId, const usize compSize, const usize compAlign) {

  /**
   * ComponentData layout:
   * - 'EcsBufferCompData' containing metadata.
   * - [PADDING] to reach the required component alignment.
   * - [PAYLOAD] the actual component data.
   */

  const usize align   = ecs_buffer_compdata_align(compAlign);
  const usize padding = ecs_buffer_compdata_padding(compAlign);
  const usize size    = bits_align(sizeof(EcsBufferCompData) + padding + compSize, align);

  Mem storage = alloc_alloc(buffer->compDataAllocator, size, align);
  diag_assert_msg(mem_valid(storage), "EcsBuffer size limit exceeded");

  EcsBufferCompData* res = mem_as_t(storage, EcsBufferCompData);
  *res                   = (EcsBufferCompData){.id = compId};
  return res;
}

EcsBuffer ecs_buffer_create(Allocator* alloc, const EcsDef* def) {
  return (EcsBuffer){
      .def      = def,
      .masks    = dynarray_create(alloc, (u16)ecs_comp_mask_size(def), ecs_comp_mask_align, 256),
      .entities = dynarray_create_t(alloc, EcsBufferEntity, 256),
      .compDataAllocator =
          alloc_chunked_create(g_alloc_page, alloc_bump_create, ecs_buffer_compdata_chunk_size),
  };
}

void ecs_buffer_destroy(EcsBuffer* buffer) {
  dynarray_destroy(&buffer->masks);
  dynarray_destroy(&buffer->entities);
  alloc_chunked_destroy(buffer->compDataAllocator);
}

void ecs_buffer_clear(EcsBuffer* buffer) {
  dynarray_clear(&buffer->masks);
  dynarray_clear(&buffer->entities);
  alloc_reset(buffer->compDataAllocator);
}

void ecs_buffer_queue_finalize_all(EcsBuffer* buffer, EcsFinalizer* finalizer) {
  for (usize i = 0; i != buffer->entities.size; ++i) {
    for (EcsBufferCompData* bufferItr = ecs_buffer_comp_begin(buffer, i); bufferItr;
         bufferItr                    = ecs_buffer_comp_next(bufferItr)) {
      void* compData = ecs_buffer_comp_data(buffer, bufferItr).ptr;
      ecs_finalizer_push(finalizer, bufferItr->id, compData);
    }
  }
}

void ecs_buffer_destroy_entity(EcsBuffer* buffer, const EcsEntityId entityId) {
  EcsBufferEntity* entity = ecs_buffer_entity_get(buffer, entityId);
  entity->flags |= EcsBufferEntityFlags_Destroy;
}

void* ecs_buffer_comp_add(
    EcsBuffer* buffer, const EcsEntityId entityId, const EcsCompId compId, const Mem data) {

  EcsBufferEntity* entity   = ecs_buffer_entity_get(buffer, entityId);
  BitSet           addMask  = ecs_buffer_mask(buffer, entity->addMask);
  const usize      compSize = ecs_def_comp_size(buffer->def, compId);

  bitset_set(addMask, compId);
  if (!compSize) {
    diag_assert(data.size == 0);
    return null; // There is no need to store payload for empty components.
  }

  // Find the last comp-data in the linked-list.
  EcsBufferCompData** last = &entity->compHead;
  for (; *last; last = &(*last)->next)
    ;

  const usize compAlign = ecs_def_comp_align(buffer->def, compId);
  *last                 = ecs_buffer_compdata_add(buffer, compId, compSize, compAlign);
  Mem payload           = ecs_buffer_compdata_payload(*last, compSize, compAlign);
  if (data.size) {
    diag_assert_msg(
        data.size == payload.size,
        "Unexpected size {} for component {} (id: {})",
        fmt_size(data.size),
        fmt_text(ecs_def_comp_name(buffer->def, compId)),
        fmt_int(compId));
    mem_cpy(payload, data);
  } else {
    mem_set(payload, 0);
  }
  return payload.ptr;
}

void ecs_buffer_comp_remove(EcsBuffer* buffer, const EcsEntityId entityId, const EcsCompId compId) {
  EcsBufferEntity* entity     = ecs_buffer_entity_get(buffer, entityId);
  BitSet           removeMask = ecs_buffer_mask(buffer, entity->removeMask);
  bitset_set(removeMask, compId);
}

usize ecs_buffer_count(const EcsBuffer* buffer) { return buffer->entities.size; }

EcsEntityId ecs_buffer_entity(const EcsBuffer* buffer, usize index) {
  return dynarray_at_t(&buffer->entities, index, EcsBufferEntity)->id;
}

EcsBufferEntityFlags ecs_buffer_entity_flags(const EcsBuffer* buffer, const usize index) {
  return dynarray_at_t(&buffer->entities, index, EcsBufferEntity)->flags;
}

BitSet ecs_buffer_entity_added(const EcsBuffer* buffer, const usize index) {
  const EcsBufferMaskId id = dynarray_at_t(&buffer->entities, index, EcsBufferEntity)->addMask;
  return ecs_buffer_mask((EcsBuffer*)buffer, id);
}

BitSet ecs_buffer_entity_removed(const EcsBuffer* buffer, const usize index) {
  const EcsBufferMaskId id = dynarray_at_t(&buffer->entities, index, EcsBufferEntity)->removeMask;
  return ecs_buffer_mask((EcsBuffer*)buffer, id);
}

EcsBufferCompData* ecs_buffer_comp_begin(const EcsBuffer* buffer, const usize index) {
  const EcsBufferEntity* entity = dynarray_at_t(&buffer->entities, index, EcsBufferEntity);
  return entity->compHead;
}

EcsBufferCompData* ecs_buffer_comp_next(const EcsBufferCompData* data) { return data->next; }

EcsCompId ecs_buffer_comp_id(const EcsBufferCompData* data) { return data->id; }

Mem ecs_buffer_comp_data(const EcsBuffer* buffer, const EcsBufferCompData* data) {
  const usize compSize  = ecs_def_comp_size(buffer->def, data->id);
  const usize compAlign = ecs_def_comp_align(buffer->def, data->id);
  return ecs_buffer_compdata_payload(data, compSize, compAlign);
}
