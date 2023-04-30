#include "asset_manager.h"
#include "asset_register.h"
#include "asset_sound.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_simd.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "snd_channel.h"
#include "snd_mixer.h"
#include "snd_register.h"

#include "constants_internal.h"
#include "device_internal.h"

#define snd_mixer_history_size 2048
ASSERT((snd_mixer_history_size & (snd_mixer_history_size - 1u)) == 0, "Non power-of-two")

#define snd_mixer_objects_max 512
ASSERT(snd_mixer_objects_max < u16_max, "Sound objects need to indexable with a 16 bit integer");

ASSERT(SndChannel_Count == 2, "Only stereo sound is supported at the moment");

#define snd_mixer_gain_adjust_per_frame 0.00075f
#define snd_mixer_pitch_adjust_per_frame 0.00025f
#define snd_mixer_pitch_min 0.1f
#define snd_mixer_limiter_release_per_frame 0.000025f
#define snd_mimer_limiter_closed_frames 1024
#define snd_mixer_limiter_max 0.75f

typedef enum {
  SndObjectPhase_Idle,
  SndObjectPhase_Setup,
  SndObjectPhase_Acquired,
  SndObjectPhase_Playing,
  SndObjectPhase_Cleanup,
} SndObjectPhase;

typedef enum {
  SndObjectFlags_Stop         = 1 << 0,
  SndObjectFlags_Looping      = 1 << 1,
  SndObjectFlags_RandomCursor = 1 << 2,
} SndObjectFlags;

typedef enum {
  SndObjectParam_Pitch,
  SndObjectParam_GainLeft,
  SndObjectParam_GainRight,
  SndObjectParam_Dummy, // Unused.

  SndObjectParam_Count,
} SndObjectParam;

ASSERT(SndObjectParam_Count == 4, "Unexpected paramater count");

typedef struct {
  SndObjectPhase phase : 8;
  SndObjectFlags flags : 8;
  u8             frameChannels;
  u16            generation; // NOTE: Expected to wrap when the object is reused often.
  u32            frameCount, frameRate;
  const f32*     samples; // f32[frameCount * frameChannels], Interleaved (LRLRLR).
  f64            cursor;  // In frames.
  ALIGNAS(16) f32 paramActual[SndObjectParam_Count];
  ALIGNAS(16) f32 paramSetting[SndObjectParam_Count];
} SndObject;

ecs_comp_define(SndMixerComp) {
  SndDevice*   device;
  f32          gainActual, gainSetting;
  f32          limiterMult;
  u32          limiterClosedFrames;
  TimeDuration lastRenderDuration;

  TimeDuration deviceTimeHead; // Timestamp of last rendered sound.

  SndObject*   objects;        // SndObject[snd_mixer_objects_max]
  String*      objectNames;    // String[snd_mixer_objects_max]
  EcsEntityId* objectAssets;   // EcsEntityId[snd_mixer_objects_max]
  u64*         objectUserData; // u64[snd_mixer_objects_max]
  BitSet       objectFreeSet;  // bit[snd_mixer_objects_max]

  /**
   * Persistent assets are pre-loaded and kept in memory at all times, this reduces the latency when
   * starting to play them.
   */
  DynArray persistentAssets;          // EcsEntityId[], sorted on the id using 'ecs_compare_entity'.
  DynArray persistentAssetsToAcquire; // EcsEntityId[], array of new persistent assets to acquire.

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
  alloc_free_array_t(g_alloc_heap, m->objectAssets, snd_mixer_objects_max);
  alloc_free_array_t(g_alloc_heap, m->objectUserData, snd_mixer_objects_max);
  alloc_free(g_alloc_heap, m->objectFreeSet);

  dynarray_destroy(&m->persistentAssets);
  dynarray_destroy(&m->persistentAssetsToAcquire);

  alloc_free_array_t(g_alloc_heap, m->historyBuffer, snd_mixer_history_size);
}

static SndMixerComp* snd_mixer_create(EcsWorld* world) {
  SndMixerComp* m = ecs_world_add_t(world, ecs_world_global(world), SndMixerComp);

  m->device      = snd_device_create(g_alloc_heap);
  m->gainSetting = 1.0f;
  m->limiterMult = 1.0f;

  m->historyBuffer = alloc_array_t(g_alloc_heap, SndBufferFrame, snd_mixer_history_size);
  mem_set(mem_create(m->historyBuffer, sizeof(SndBufferFrame) * snd_mixer_history_size), 0);

  m->objects = alloc_array_t(g_alloc_heap, SndObject, snd_mixer_objects_max);
  mem_set(mem_create(m->objects, sizeof(SndObject) * snd_mixer_objects_max), 0);

  m->objectNames = alloc_array_t(g_alloc_heap, String, snd_mixer_objects_max);
  mem_set(mem_create(m->objectNames, sizeof(String) * snd_mixer_objects_max), 0);

  m->objectAssets = alloc_array_t(g_alloc_heap, EcsEntityId, snd_mixer_objects_max);
  mem_set(mem_create(m->objectAssets, sizeof(EcsEntityId) * snd_mixer_objects_max), 0);

  m->objectUserData = alloc_array_t(g_alloc_heap, u64, snd_mixer_objects_max);
  mem_set(mem_create(m->objectUserData, sizeof(u64) * snd_mixer_objects_max), 0xFF);

  m->objectFreeSet = alloc_alloc(g_alloc_heap, bits_to_bytes(snd_mixer_objects_max), 1);
  bitset_set_all(m->objectFreeSet, snd_mixer_objects_max);

  m->persistentAssets          = dynarray_create_t(g_alloc_heap, EcsEntityId, 64);
  m->persistentAssetsToAcquire = dynarray_create_t(g_alloc_heap, EcsEntityId, 8);

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

static void snd_mixer_history_update(SndMixerComp* m, const SndChannel channel, const f32 value) {
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

ecs_view_define(MixerView) { ecs_access_write(SndMixerComp); }

ecs_system_define(SndMixerUpdateSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), SndMixerComp)) {
    snd_mixer_create(world);
    return;
  }
  SndMixerComp* m = ecs_utils_write_t(world, MixerView, ecs_world_global(world), SndMixerComp);

  /**
   * Acquire new persistent sound assets.
   * TODO: Support reloading persistent assets when they are changed.
   */
  dynarray_for_t(&m->persistentAssetsToAcquire, EcsEntityId, a) { asset_acquire(world, *a); }
  dynarray_clear(&m->persistentAssetsToAcquire);

  /**
   * Update sound objects.
   */

  EcsView*     assetView = ecs_world_view_t(world, AssetView);
  EcsIterator* assetItr  = ecs_view_itr(assetView);

  for (u32 i = 0; i != snd_mixer_objects_max; ++i) {
    SndObject* obj = &m->objects[i];
    switch (obj->phase) {
    case SndObjectPhase_Idle:
    case SndObjectPhase_Playing:
      continue;
    case SndObjectPhase_Setup:
      if (m->objectAssets[i]) {
        asset_acquire(world, m->objectAssets[i]);
        obj->phase = SndObjectPhase_Acquired;
      }
      /**
       * An 'asset_acquire()' takes one tick to take effect as it requires the ecs to be flushed
       * and then the asset update to happen. Before this time the asset could be loaded at the
       * moment but been queued for unload the next tick.
       *
       * To avoid introducing an additional frame of delay even if its already loaded we don't wait
       * but we do check if the ref-count is zero when accessing the asset. If its zero then its not
       * safe to access the asset as it might be queued for unload.
       */
      ASSERT(SndOrder_Update > AssetOrder_Update, "Sound update has to happen after asset update");
      // Fallthrough.
    case SndObjectPhase_Acquired:
      if (obj->flags & SndObjectFlags_Stop) {
        obj->phase = SndObjectPhase_Cleanup;
        // Fallthrough.
      } else if (ecs_view_maybe_jump(assetItr, m->objectAssets[i])) {
        const AssetComp* asset = ecs_view_read_t(assetItr, AssetComp);
        if (asset_ref_count(asset) == 0) {
          continue; // Our acquire has not been processed; unsafe to access (see comment above).
        }
        m->objectNames[i] = asset_id(asset);

        const AssetSoundComp* soundAsset = ecs_view_read_t(assetItr, AssetSoundComp);
        obj->frameChannels               = soundAsset->frameChannels;
        obj->frameCount                  = soundAsset->frameCount;
        obj->frameRate                   = soundAsset->frameRate;
        obj->samples                     = soundAsset->samples;
        obj->phase                       = SndObjectPhase_Playing;

        if (obj->flags & SndObjectFlags_RandomCursor) {
          obj->cursor = rng_sample_range(g_rng, 0.0, (f64)obj->frameCount);
        }

        if (!(obj->flags & SndObjectFlags_Looping)) {
          // Start playing at the desired gain (as opposed to looping sounds which will fade-in).
          obj->paramActual[SndObjectParam_GainLeft]  = obj->paramSetting[SndObjectParam_GainLeft];
          obj->paramActual[SndObjectParam_GainRight] = obj->paramSetting[SndObjectParam_GainRight];
        }

        continue; // Ready for playback.
      } else if (ecs_world_has_t(world, m->objectAssets[i], AssetFailedComp)) {
        log_e("Failed to sound resource");
        obj->phase = SndObjectPhase_Cleanup;
        // Fallthrough.
      } else {
        continue; // Wait for the asset to load (or to fail).
      }
    case SndObjectPhase_Cleanup:
      asset_release(world, m->objectAssets[i]);
      snd_object_release(m, obj);
      *obj                 = (SndObject){.generation = obj->generation};
      m->objectNames[i]    = string_empty;
      m->objectAssets[i]   = 0;
      m->objectUserData[i] = sentinel_u64;
      continue;
    }
    UNREACHABLE
  }
}

INLINE_HINT static void snd_object_sample(
    const SndObject* obj, const f64 frame, f32 out[PARAM_ARRAY_SIZE(SndChannel_Count)]) {
  /**
   * Naive sampling using linear interpolation between the two closest samples.
   * This works reasonably for up-sampling (even though we should consider methods that preserve the
   * curve better, like Hermite interpolation), but for down-sampling this ignores the aliasing that
   * occurs with frequencies that we cannot represent.
   */
  const u32 edgeA  = math_min(obj->frameCount - 2, (u32)frame);
  const u32 edgeB  = edgeA + 1;
  const f32 frac   = (f32)(frame - edgeA);
  const u32 indexA = edgeA * obj->frameChannels;
  const u32 indexB = edgeB * obj->frameChannels;

  // Left channel.
  {
    const f32 valA = obj->samples[indexA + SndChannel_Left];
    const f32 valB = obj->samples[indexB + SndChannel_Left];
    out[0]         = math_lerp(valA, valB, frac);
  }

  // Right channel.
  if (obj->frameChannels > 1) {
    const f32 valA = obj->samples[indexA + SndChannel_Right];
    const f32 valB = obj->samples[indexB + SndChannel_Right];
    out[1]         = math_lerp(valA, valB, frac);
  } else {
    out[1] = out[0];
  }
}

INLINE_HINT static SimdVec snd_object_param_blend(
    const SimdVec actual, const SimdVec target, const SimdVec deltaMin, const SimdVec deltaMax) {
  const SimdVec delta        = simd_vec_sub(target, actual);
  const SimdVec deltaClamped = simd_vec_max(simd_vec_min(delta, deltaMax), deltaMin);
  return simd_vec_add(actual, deltaClamped);
}

static bool snd_object_render(SndObject* obj, SndBuffer out) {
  diag_assert(obj->phase == SndObjectPhase_Playing);

  const f64  advancePerFrame = obj->frameRate / (f64)out.frameRate;
  const bool pitchTooLow     = obj->paramSetting[SndObjectParam_Pitch] <= snd_mixer_pitch_min;

  ALIGNAS(16)
  static const f32 g_paramDeltaMaxValues[SndObjectParam_Count] = {
      [SndObjectParam_Pitch]     = snd_mixer_pitch_adjust_per_frame,
      [SndObjectParam_GainLeft]  = snd_mixer_gain_adjust_per_frame,
      [SndObjectParam_GainRight] = snd_mixer_gain_adjust_per_frame,
  };

  ALIGNAS(16)
  const f32 paramMultValues[SndObjectParam_Count] = {
      [SndObjectParam_Pitch]     = 1.0f,
      [SndObjectParam_GainLeft]  = pitchTooLow ? 0.0f : 1.0f,
      [SndObjectParam_GainRight] = pitchTooLow ? 0.0f : 1.0f,
  };

  const SimdVec paramDeltaMax = simd_vec_load(g_paramDeltaMaxValues);
  const SimdVec paramDeltaMin = simd_vec_mul(paramDeltaMax, simd_vec_broadcast(-1.0f));
  const SimdVec paramMult     = simd_vec_load(paramMultValues);
  const SimdVec paramTarget   = simd_vec_mul(simd_vec_load(obj->paramSetting), paramMult);
  SimdVec       paramActual   = simd_vec_load(obj->paramActual);

  for (u32 frame = 0; frame != out.frameCount; ++frame) {
    paramActual = snd_object_param_blend(paramActual, paramTarget, paramDeltaMin, paramDeltaMax);

    f32 samples[SndChannel_Count];
    snd_object_sample(obj, obj->cursor, samples);

    const f32 gainLeft = simd_vec_x(simd_vec_splat(paramActual, SndObjectParam_GainLeft));
    out.frames[frame].samples[SndChannel_Left] += samples[SndChannel_Left] * gainLeft;

    const f32 gainRight = simd_vec_x(simd_vec_splat(paramActual, SndObjectParam_GainRight));
    out.frames[frame].samples[SndChannel_Right] += samples[SndChannel_Right] * gainRight;

    ASSERT(SndObjectParam_Pitch == 0, "Expected pitch to be the first parameter");
    obj->cursor += advancePerFrame * (f64)simd_vec_x(paramActual);

    if (UNLIKELY(obj->cursor >= obj->frameCount)) {
      if (obj->flags & SndObjectFlags_Looping) {
        obj->cursor -= obj->frameCount;
      } else {
        return false; // Finished playing.
      }
    }
  }

  simd_vec_store(paramActual, obj->paramActual);
  return true; // Still playing.
}

static bool snd_object_skip(SndObject* obj, const TimeDuration dur) {
  diag_assert(obj->phase == SndObjectPhase_Playing);

  const bool pitchTooLow = obj->paramSetting[SndObjectParam_Pitch] <= snd_mixer_pitch_min;
  const f64  durSeconds  = (f64)dur / (f64)time_second;
  const f64  durFrames   = durSeconds * snd_frame_rate;

  ALIGNAS(16)
  const f32 paramDeltaMaxValues[SndObjectParam_Count] = {
      [SndObjectParam_Pitch]     = (f32)(durFrames * snd_mixer_pitch_adjust_per_frame),
      [SndObjectParam_GainLeft]  = (f32)(durFrames * snd_mixer_gain_adjust_per_frame),
      [SndObjectParam_GainRight] = (f32)(durFrames * snd_mixer_gain_adjust_per_frame),
  };

  ALIGNAS(16)
  const f32 paramMultValues[SndObjectParam_Count] = {
      [SndObjectParam_Pitch]     = 1.0f,
      [SndObjectParam_GainLeft]  = pitchTooLow ? 0.0f : 1.0f,
      [SndObjectParam_GainRight] = pitchTooLow ? 0.0f : 1.0f,
  };

  const SimdVec paramDeltaMax = simd_vec_load(paramDeltaMaxValues);
  const SimdVec paramDeltaMin = simd_vec_mul(paramDeltaMax, simd_vec_broadcast(-1.0f));
  const SimdVec paramMult     = simd_vec_load(paramMultValues);
  const SimdVec paramTarget   = simd_vec_mul(simd_vec_load(obj->paramSetting), paramMult);

  SimdVec paramActual = simd_vec_load(obj->paramActual);
  paramActual = snd_object_param_blend(paramActual, paramTarget, paramDeltaMin, paramDeltaMax);
  simd_vec_store(paramActual, obj->paramActual);

  obj->cursor += durSeconds * obj->frameRate;

  if (obj->cursor >= obj->frameCount) {
    if (obj->flags & SndObjectFlags_Looping) {
      obj->cursor = math_mod_f64(obj->cursor, (f64)obj->frameCount);
    } else {
      return false; // Finished playing.
    }
  }

  return true; // Still playing.
}

static bool snd_object_is_muted(const SndObject* obj) {
  const bool pitchTooLow = obj->paramSetting[SndObjectParam_Pitch] <= snd_mixer_pitch_min;
  const f32  gainMult    = pitchTooLow ? 0.0f : 1.0f;
  if ((obj->paramSetting[SndObjectParam_GainLeft] * gainMult) > f32_epsilon) {
    return false;
  }
  if ((obj->paramSetting[SndObjectParam_GainRight] * gainMult) > f32_epsilon) {
    return false;
  }
  return true;
}

static bool snd_object_is_silent(const SndObject* obj) {
  if (obj->paramActual[SndObjectParam_GainLeft] > f32_epsilon) {
    return false;
  }
  if (obj->paramActual[SndObjectParam_GainRight] > f32_epsilon) {
    return false;
  }
  return true;
}

static void snd_mixer_write_to_device(
    SndMixerComp* m, const SndDevicePeriod devicePeriod, const SndBuffer buffer) {
  diag_assert(devicePeriod.frameCount == buffer.frameCount);

  const f32 limiterThreshold =
      math_min(snd_mixer_limiter_max * m->gainSetting, snd_mixer_limiter_max);

  for (u32 frame = 0; frame != devicePeriod.frameCount; ++frame) {
    const f32 gainTarget = m->gainSetting * m->limiterMult;
    math_towards_f32(&m->gainActual, gainTarget, snd_mixer_gain_adjust_per_frame);

    if (m->limiterClosedFrames) {
      --m->limiterClosedFrames;
    } else {
      math_towards_f32(&m->limiterMult, 1.0f, snd_mixer_limiter_release_per_frame);
    }

    for (SndChannel channel = 0; channel != SndChannel_Count; ++channel) {
      const f32 val = buffer.frames[frame].samples[channel] * m->gainActual;

      // Engage the limiter if the value exceeds the threshold.
      if (val > limiterThreshold) {
        m->limiterMult         = math_min(m->limiterMult, limiterThreshold / val);
        m->limiterClosedFrames = snd_mimer_limiter_closed_frames;
      }

      // Add it to the history ring-buffer for analysis / debug purposes.
      snd_mixer_history_update(m, channel, val);

      // Write to the device buffer.
      const i16 clipped = val > 1.0 ? i16_max : (val < -1.0 ? i16_min : (i16)(val * i16_max));
      devicePeriod.samples[frame * SndChannel_Count + channel] = clipped;
    }
    snd_mixer_history_advance(m);
  }
}

ecs_system_define(SndMixerRenderSys) {
  EcsView*     mixerView = ecs_world_view_t(world, MixerView);
  EcsIterator* mixerItr  = ecs_view_maybe_at(mixerView, ecs_world_global(world));
  if (!mixerItr) {
    return;
  }
  SndMixerComp* m = ecs_view_write_t(mixerItr, SndMixerComp);

  SndBufferFrame soundFrames[snd_frame_count_max] = {0};

  const TimeSteady renderStartTime = time_steady_clock();
  if (snd_device_begin(m->device)) {
    const SndDevicePeriod period         = snd_device_period(m->device);
    const TimeDuration    periodDuration = period.frameCount * time_second / snd_frame_rate;

    diag_assert(period.frameCount <= snd_frame_count_max);
    const SndBuffer soundBuffer = {
        .frames     = soundFrames,
        .frameCount = period.frameCount,
        .frameRate  = snd_frame_rate,
    };

    // Skip sounds forward if there's a gap between the end of the last rendered sound and the new
    // period, can happen when there was a device buffer underrun.
    if (period.timeBegin > m->deviceTimeHead) {
      const TimeDuration skipDur = period.timeBegin - m->deviceTimeHead;
      log_d("Sound-mixer skip", log_param("duration", fmt_duration(skipDur)));
      for (u32 i = 0; i != snd_mixer_objects_max; ++i) {
        SndObject* obj = &m->objects[i];
        if (obj->phase == SndObjectPhase_Playing && !snd_object_skip(obj, skipDur)) {
          ++obj->phase; // Object is finished playing after the skip duration.
        }
      }
    }

    // Render all objects into the soundBuffer.
    for (u32 i = 0; i != snd_mixer_objects_max; ++i) {
      SndObject* obj = &m->objects[i];
      if (obj->phase != SndObjectPhase_Playing) {
        continue;
      }
      const bool muted  = snd_object_is_muted(obj);
      const bool silent = snd_object_is_silent(obj);

      if (muted && silent) {
        if (obj->flags & SndObjectFlags_Stop) {
          goto FinishedPlaying; // Stopped and finished fading out.
        }
        if (!snd_object_skip(obj, periodDuration)) {
          goto FinishedPlaying;
        }
      } else {
        if (!snd_object_render(obj, soundBuffer)) {
          goto FinishedPlaying;
        }
      }
      continue;

    FinishedPlaying:
      ++obj->phase;
      continue;
    }

    // Write the soundBuffer to the device.
    snd_mixer_write_to_device(m, period, soundBuffer);
    snd_device_end(m->device);

    m->lastRenderDuration = time_steady_duration(renderStartTime, time_steady_clock());
    m->deviceTimeHead     = period.timeBegin + periodDuration;
  }
}

ecs_module_init(snd_mixer_module) {
  ecs_register_comp(SndMixerComp, .destructor = ecs_destruct_mixer_comp);

  ecs_register_view(AssetView);
  ecs_register_view(MixerView);

  ecs_register_system(SndMixerUpdateSys, ecs_view_id(MixerView), ecs_view_id(AssetView));
  ecs_register_system(SndMixerRenderSys, ecs_view_id(MixerView));

  ecs_order(SndMixerUpdateSys, SndOrder_Update);
  ecs_order(SndMixerRenderSys, SndOrder_Render);
}

SndResult snd_object_new(SndMixerComp* m, SndObjectId* outId) {
  const SndObjectId id  = snd_object_acquire(m);
  SndObject*        obj = snd_object_get(m, id);
  if (UNLIKELY(!obj)) {
    return SndResult_FailedToAcquireObject;
  }
  obj->phase                                  = SndObjectPhase_Setup;
  obj->paramActual[SndObjectParam_Pitch]      = 1.0f;
  obj->paramSetting[SndObjectParam_Pitch]     = 1.0f;
  obj->paramSetting[SndObjectParam_GainLeft]  = 1.0f;
  obj->paramSetting[SndObjectParam_GainRight] = 1.0f;
  m->objectUserData[snd_object_id_index(id)]  = 0;

  *outId = id;
  return SndResult_Success;
}

SndResult snd_object_stop(SndMixerComp* m, const SndObjectId id) {
  SndObject* obj = snd_object_get(m, id);
  if (UNLIKELY(!obj)) {
    return SndResult_InvalidObject;
  }
  obj->flags |= SndObjectFlags_Stop;
  obj->paramSetting[SndObjectParam_GainLeft]  = 0.0f;
  obj->paramSetting[SndObjectParam_GainRight] = 0.0f;
  return SndResult_Success;
}

bool snd_object_is_active(const SndMixerComp* m, const SndObjectId id) {
  return snd_object_get_readonly(m, id) != null;
}

bool snd_object_is_loading(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj && obj->phase != SndObjectPhase_Playing;
}

u64 snd_object_get_user_data(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? m->objectUserData[snd_object_id_index(id)] : sentinel_u64;
}

String snd_object_get_name(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? m->objectNames[snd_object_id_index(id)] : string_empty;
}

u32 snd_object_get_frame_count(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? obj->frameCount : 0;
}

u32 snd_object_get_frame_rate(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? obj->frameRate : 0;
}

u8 snd_object_get_frame_channels(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? obj->frameChannels : 0;
}

f64 snd_object_get_cursor(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? obj->cursor : 0.0;
}

f32 snd_object_get_pitch(const SndMixerComp* m, const SndObjectId id) {
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? obj->paramActual[SndObjectParam_Pitch] : 0.0f;
}

f32 snd_object_get_gain(const SndMixerComp* m, const SndObjectId id, const SndChannel chan) {
  diag_assert(chan < SndChannel_Count);
  const SndObject* obj = snd_object_get_readonly(m, id);
  return obj ? obj->paramActual[SndObjectParam_GainLeft + chan] : 0.0f;
}

SndResult snd_object_set_asset(SndMixerComp* m, const SndObjectId id, const EcsEntityId asset) {
  SndObject* obj = snd_object_get(m, id);
  if (UNLIKELY(!obj || obj->phase != SndObjectPhase_Setup)) {
    return SndResult_InvalidObjectPhase;
  }
  m->objectAssets[snd_object_id_index(id)] = asset;
  return SndResult_Success;
}

SndResult snd_object_set_user_data(SndMixerComp* m, const SndObjectId id, const u64 userData) {
  SndObject* obj = snd_object_get(m, id);
  if (UNLIKELY(!obj || obj->phase != SndObjectPhase_Setup)) {
    return SndResult_InvalidObjectPhase;
  }
  m->objectUserData[snd_object_id_index(id)] = userData;
  return SndResult_Success;
}

SndResult snd_object_set_looping(SndMixerComp* m, const SndObjectId id) {
  SndObject* obj = snd_object_get(m, id);
  if (UNLIKELY(!obj || obj->phase != SndObjectPhase_Setup)) {
    return SndResult_InvalidObjectPhase;
  }
  obj->flags |= SndObjectFlags_Looping;
  return SndResult_Success;
}

SndResult snd_object_set_random_cursor(SndMixerComp* m, const SndObjectId id) {
  SndObject* obj = snd_object_get(m, id);
  if (UNLIKELY(!obj || obj->phase != SndObjectPhase_Setup)) {
    return SndResult_InvalidObjectPhase;
  }
  obj->flags |= SndObjectFlags_RandomCursor;
  return SndResult_Success;
}

SndResult snd_object_set_pitch(SndMixerComp* m, const SndObjectId id, const f32 pitch) {
  SndObject* obj = snd_object_get(m, id);
  if (UNLIKELY(!obj)) {
    return SndResult_InvalidObject;
  }
  if (UNLIKELY(pitch < 0.0f || pitch > 10.0f)) {
    return SndResult_ParameterOutOfRange;
  }
  if (obj->phase == SndObjectPhase_Setup) {
    obj->paramActual[SndObjectParam_Pitch] = pitch;
  }
  obj->paramSetting[SndObjectParam_Pitch] = pitch;
  return SndResult_Success;
}

SndResult
snd_object_set_gain(SndMixerComp* m, const SndObjectId id, const SndChannel chan, const f32 gain) {
  diag_assert(chan < SndChannel_Count);
  SndObject* obj = snd_object_get(m, id);
  if (UNLIKELY(!obj)) {
    return SndResult_InvalidObject;
  }
  if (UNLIKELY(gain < 0.0f || gain > 10.0f)) {
    return SndResult_ParameterOutOfRange;
  }
  if (UNLIKELY(obj->flags & SndObjectFlags_Stop)) {
    return SndResult_ObjectStopped;
  }
  obj->paramSetting[SndObjectParam_GainLeft + chan] = gain;
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

void snd_mixer_persistent_asset(SndMixerComp* m, const EcsEntityId asset) {
  DynArray*    arr  = &m->persistentAssets;
  EcsEntityId* slot = dynarray_find_or_insert_sorted(arr, ecs_compare_entity, &asset);

  // Check if this was the first time this asset was marked persistent.
  if (!ecs_entity_valid(*slot)) {
    *slot                                                        = asset;
    *dynarray_push_t(&m->persistentAssetsToAcquire, EcsEntityId) = asset;
  }
}

f32 snd_mixer_gain_get(const SndMixerComp* m) { return m->gainSetting; }

SndResult snd_mixer_gain_set(SndMixerComp* m, const f32 gain) {
  if (UNLIKELY(gain < 0.0f || gain > 10.0f)) {
    return SndResult_ParameterOutOfRange;
  }
  m->gainSetting = gain;
  return SndResult_Success;
}

f32 snd_mixer_limiter_get(const SndMixerComp* m) { return m->limiterMult; }

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
