#include "asset_manager.h"
#include "asset_sound.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "snd_channel.h"
#include "snd_mixer.h"
#include "snd_register.h"

#include "constants_internal.h"
#include "device_internal.h"

#define snd_mixer_history_size 2048
ASSERT((snd_mixer_history_size & (snd_mixer_history_size - 1u)) == 0, "Non power-of-two")

#define snd_mixer_objects_max 2048
ASSERT(snd_mixer_objects_max < u16_max, "Sound objects need to indexable with a 16 bit integer");

#define snd_mixer_gain_adjust_per_frame 0.0001f

typedef enum {
  SndObjectPhase_Idle,
  SndObjectPhase_Setup,
  SndObjectPhase_Acquired,
  SndObjectPhase_Playing,
  SndObjectPhase_Cleanup,
} SndObjectPhase;

typedef struct {
  SndObjectPhase phase : 8;
  u8             frameChannels;
  u16            generation; // NOTE: Expected to wrap when the object is reused often.
  u32            frameCount, frameRate;
  const f32*     samples;    // f32[frameCount * frameChannels], Interleaved (LRLRLR).
  f64            playCursor; // In frames.
  EcsEntityId    asset;
} SndObject;

ecs_comp_define(SndMixerComp) {
  SndDevice*   device;
  f32          gainActual, gainTarget;
  TimeDuration lastRenderDuration;

  SndObject* objects;       // SndObject[snd_mixer_objects_max]
  String*    objectNames;   // String[snd_mixer_objects_max]
  BitSet     objectFreeSet; // bit[snd_mixer_objects_max]

  /**
   * Keep a history of the last N frames in a ring-buffer for analysis and debug purposes.
   */
  SndBufferFrame* historyBuffer;
  usize           historyCursor;
};

static void ecs_destruct_mixer_comp(void* data) {
  SndMixerComp* m = data;
  snd_device_destroy(m->device);

  alloc_free_array_t(g_alloc_heap, m->objects, snd_mixer_objects_max);
  alloc_free_array_t(g_alloc_heap, m->objectNames, snd_mixer_objects_max);
  alloc_free(g_alloc_heap, m->objectFreeSet);

  alloc_free_array_t(g_alloc_heap, m->historyBuffer, snd_mixer_history_size);
}

static SndMixerComp* snd_mixer_create(EcsWorld* world) {
  SndMixerComp* m = ecs_world_add_t(world, ecs_world_global(world), SndMixerComp);

  m->device     = snd_device_create(g_alloc_heap);
  m->gainTarget = 1.0f;

  m->historyBuffer = alloc_array_t(g_alloc_heap, SndBufferFrame, snd_mixer_history_size);
  mem_set(mem_create(m->historyBuffer, sizeof(SndBufferFrame) * snd_mixer_history_size), 0);

  m->objects     = alloc_array_t(g_alloc_heap, SndObject, snd_mixer_objects_max);
  m->objectNames = alloc_array_t(g_alloc_heap, String, snd_mixer_objects_max);
  mem_set(mem_create(m->objects, sizeof(SndObject) * snd_mixer_objects_max), 0);
  mem_set(mem_create(m->objectNames, sizeof(String) * snd_mixer_objects_max), 0);

  m->objectFreeSet = alloc_alloc(g_alloc_heap, bits_to_bytes(snd_mixer_objects_max), 1);
  bitset_set_all(m->objectFreeSet, snd_mixer_objects_max);

  return m;
}

static u16 snd_object_id_index(const SndObjectId id) { return (u16)id; }
static u16 snd_object_id_generation(const SndObjectId id) { return (u16)(id >> 16); }

static SndObjectId snd_object_id_create(const u16 index, const u16 generation) {
  return (SndObjectId)index | ((SndObjectId)generation << 16);
}

static SndObject* snd_object_get(SndMixerComp* m, const SndObjectId id) {
  const u16 index = snd_object_id_index(id);
  if (index >= snd_mixer_objects_max) {
    return null;
  }
  SndObject* obj = &m->objects[index];
  if (obj->generation != snd_object_id_generation(id)) {
    return null;
  }
  return obj;
}

static const SndObject* snd_object_get_readonly(const SndMixerComp* m, const SndObjectId id) {
  return snd_object_get((SndMixerComp*)m, id);
}

static SndObjectId snd_object_acquire(SndMixerComp* m) {
  const usize index = bitset_next(m->objectFreeSet, 0);
  if (UNLIKELY(sentinel_check(index))) {
    return (SndObjectId)sentinel_u16;
  }
  bitset_clear(m->objectFreeSet, index);
  SndObject* obj = &m->objects[index];
  ++obj->generation; // NOTE: Expected to wrap when the object is reused often.
  return snd_object_id_create((u16)index, obj->generation);
}

static void snd_object_release(SndMixerComp* m, const SndObject* obj) {
  const u16 index = (u16)(obj - m->objects);
  diag_assert_msg(!bitset_test(m->objectFreeSet, index), "Object double free");
  bitset_set(m->objectFreeSet, index);
}

static u32 snd_object_count_in_phase(const SndMixerComp* m, const SndObjectPhase phase) {
  u32 count = 0;
  for (u32 i = 0; i != snd_mixer_objects_max; ++i) {
    if (m->objects[i].phase == phase) {
      ++count;
    }
  }
  return count;
}

static void snd_mixer_history_set(SndMixerComp* m, const SndChannel channel, const f32 value) {
  m->historyBuffer[m->historyCursor].samples[channel] = value;
}

static void snd_mixer_history_advance(SndMixerComp* m) {
  m->historyCursor = (m->historyCursor + 1) & (snd_mixer_history_size - 1);
}

ecs_view_define(AssetView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetSoundComp);
  ecs_access_with(AssetLoadedComp);
}

ecs_view_define(GlobalUpdateView) { ecs_access_write(SndMixerComp); }

ecs_system_define(SndMixerUpdateSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), SndMixerComp)) {
    snd_mixer_create(world);
    return;
  }
  EcsView*      globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator*  globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  SndMixerComp* m          = ecs_view_write_t(globalItr, SndMixerComp);

  EcsView*     assetView = ecs_world_view_t(world, AssetView);
  EcsIterator* assetItr  = ecs_view_itr(assetView);

  for (u32 i = 0; i != snd_mixer_objects_max; ++i) {
    SndObject* obj = &m->objects[i];
    switch (obj->phase) {
    case SndObjectPhase_Idle:
    case SndObjectPhase_Playing:
      continue;
    case SndObjectPhase_Setup:
      if (obj->asset) {
        asset_acquire(world, obj->asset);
        obj->phase = SndObjectPhase_Acquired;
        // Fallthrough.
      } else {
        continue;
      }
    case SndObjectPhase_Acquired:
      if (ecs_view_maybe_jump(assetItr, obj->asset)) {
        const AssetSoundComp* soundAsset = ecs_view_read_t(assetItr, AssetSoundComp);
        obj->frameChannels               = soundAsset->frameChannels;
        obj->frameCount                  = soundAsset->frameCount;
        obj->frameRate                   = soundAsset->frameRate;
        obj->samples                     = soundAsset->samples;
        obj->phase                       = SndObjectPhase_Playing;

        const AssetComp* asset = ecs_view_read_t(assetItr, AssetComp);
        m->objectNames[i]      = asset_id(asset);
      }
      continue;
    case SndObjectPhase_Cleanup:
      asset_release(world, obj->asset);
      snd_object_release(m, obj);
      *obj              = (SndObject){.generation = obj->generation};
      m->objectNames[i] = string_empty;
      continue;
    }
    UNREACHABLE
  }
}

INLINE_HINT static f32 snd_object_sample(const SndObject* obj, SndChannel chan, const f64 frame) {
  if (chan >= obj->frameChannels) {
    chan = SndChannel_Left;
  }
  /**
   * Naive sampling using linear interpolation between the two closest samples.
   * This works reasonably for up-sampling (even though we should consider methods that preserve the
   * curve better, like Hermite interpolation), but for down-sampling this ignores the aliasing that
   * occurs with frequencies that we cannot represent.
   */
  const u32 edgeA = math_min(obj->frameCount - 2, (u32)frame);
  const u32 edgeB = edgeA + 1;
  const f32 valA  = obj->samples[edgeA * obj->frameChannels + chan];
  const f32 valB  = obj->samples[edgeB * obj->frameChannels + chan];
  return math_lerp(valA, valB, (f32)(frame - edgeA));
}

static bool snd_object_render(SndObject* obj, SndBuffer out) {
  diag_assert(obj->phase == SndObjectPhase_Playing);

  const f64 advancePerFrame = obj->frameRate / (f64)out.frameRate;
  for (u32 frame = 0; frame != out.frameCount; ++frame) {
    for (SndChannel chan = 0; chan != SndChannel_Count; ++chan) {
      out.frames[frame].samples[chan] += snd_object_sample(obj, chan, obj->playCursor);
    }
    obj->playCursor += advancePerFrame;
    if (obj->playCursor >= obj->frameCount) {
      return false; // Finished playing.
    }
  }
  return true; // Still playing.
}

static void snd_mixer_fill_device_period(
    SndMixerComp* m, const SndDevicePeriod devicePeriod, const SndBuffer buffer) {
  diag_assert(devicePeriod.frameCount == buffer.frameCount);

  for (u32 frame = 0; frame != devicePeriod.frameCount; ++frame) {
    math_towards_f32(&m->gainActual, m->gainTarget, snd_mixer_gain_adjust_per_frame);

    for (SndChannel channel = 0; channel != SndChannel_Count; ++channel) {
      const f32 val = buffer.frames[frame].samples[channel] * m->gainActual;

      // Add it to the history ring-buffer for analysis / debug purposes.
      snd_mixer_history_set(m, channel, val);

      // Write to the device buffer.
      const i16 clipped = val > 1.0 ? i16_max : (val < -1.0 ? i16_min : (i16)(val * i16_max));
      devicePeriod.samples[frame * SndChannel_Count + channel] = clipped;
    }
    snd_mixer_history_advance(m);
  }
}

ecs_view_define(GlobalRenderView) { ecs_access_write(SndMixerComp); }

ecs_system_define(SndMixerRenderSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalRenderView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndMixerComp* m = ecs_view_write_t(globalItr, SndMixerComp);

  if (snd_device_begin(m->device)) {
    const TimeSteady      renderStartTime = time_steady_clock();
    const SndDevicePeriod period          = snd_device_period(m->device);

    SndBufferFrame  soundFrames[snd_frame_count_max] = {0};
    const SndBuffer soundBuffer                      = {
        .frames     = soundFrames,
        .frameCount = period.frameCount,
        .frameRate  = snd_frame_rate,
    };

    // Render all objects into the soundBuffer.
    for (u32 i = 0; i != snd_mixer_objects_max; ++i) {
      SndObject* obj = &m->objects[i];
      if (obj->phase == SndObjectPhase_Playing) {
        if (!snd_object_render(obj, soundBuffer)) {
          ++obj->phase;
        }
      }
    }

    // Write the soundBuffer to the device.
    snd_mixer_fill_device_period(m, period, soundBuffer);

    snd_device_end(m->device);
    m->lastRenderDuration = time_steady_duration(renderStartTime, time_steady_clock());
  }
}

ecs_module_init(snd_mixer_module) {
  ecs_register_comp(SndMixerComp, .destructor = ecs_destruct_mixer_comp);

  ecs_register_view(AssetView);
  ecs_register_view(GlobalUpdateView);
  ecs_register_view(GlobalRenderView);

  ecs_register_system(SndMixerUpdateSys, ecs_view_id(GlobalUpdateView), ecs_view_id(AssetView));
  ecs_register_system(SndMixerRenderSys, ecs_view_id(GlobalRenderView));

  ecs_order(SndMixerRenderSys, SndOrder_Render);
}

SndResult snd_object_new(SndMixerComp* m, SndObjectId* outId) {
  const SndObjectId id  = snd_object_acquire(m);
  SndObject*        obj = snd_object_get(m, id);
  if (UNLIKELY(!obj)) {
    return SndResult_FailedToAcquireObject;
  }
  obj->phase = SndObjectPhase_Setup;
  *outId     = id;
  return SndResult_Success;
}

String snd_object_get_name(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? m->objectNames[snd_object_id_index(id)] : string_empty;
}

bool snd_object_get_loading(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj && obj->phase != SndObjectPhase_Playing;
}

TimeDuration snd_object_get_duration(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? (obj->frameCount * time_second / obj->frameRate) : time_seconds(0);
}

u32 snd_object_get_frame_rate(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? obj->frameRate : 0;
}

u8 snd_object_get_frame_channels(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? obj->frameChannels : 0;
}

SndResult snd_object_set_asset(SndMixerComp* m, const SndObjectId id, const EcsEntityId asset) {
  SndObject* obj = snd_object_get(m, id);
  if (UNLIKELY(!obj || obj->phase != SndObjectPhase_Setup)) {
    return SndResult_InvalidObjectPhase;
  }
  obj->asset = asset;
  return SndResult_Success;
}

SndObjectId snd_object_next(const SndMixerComp* m, const SndObjectId previousId) {
  u16 index = sentinel_check(previousId) ? 0 : (snd_object_id_index(previousId) + 1);
  for (; index < snd_mixer_objects_max; ++index) {
    const SndObject* obj = &m->objects[index];
    if (obj->phase != SndObjectPhase_Idle) {
      return snd_object_id_create(index, obj->generation);
    }
  }
  return sentinel_u32;
}

f32  snd_mixer_gain_get(const SndMixerComp* m) { return m->gainTarget; }
void snd_mixer_gain_set(SndMixerComp* m, const f32 gain) { m->gainTarget = gain; }

String snd_mixer_device_id(const SndMixerComp* m) { return snd_device_id(m->device); }

String snd_mixer_device_backend(const SndMixerComp* m) { return snd_device_backend(m->device); }

String snd_mixer_device_state(const SndMixerComp* m) {
  const SndDeviceState state = snd_device_state(m->device);
  return snd_device_state_str(state);
}

u64 snd_mixer_device_underruns(const SndMixerComp* m) { return snd_device_underruns(m->device); }

u32 snd_mixer_objects_playing(const SndMixerComp* m) {
  return snd_object_count_in_phase(m, SndObjectPhase_Playing);
}

u32 snd_mixer_objects_allocated(const SndMixerComp* m) {
  const usize freeObjects = bitset_count(m->objectFreeSet);
  return snd_mixer_objects_max - (u32)freeObjects;
}

TimeDuration snd_mixer_render_duration(const SndMixerComp* m) { return m->lastRenderDuration; }

SndBufferView snd_mixer_history(const SndMixerComp* m) {
  return (SndBufferView){
      .frames     = m->historyBuffer,
      .frameCount = snd_mixer_history_size,
      .frameRate  = snd_frame_rate};
}
