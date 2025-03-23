#include "core_alloc.h"
#include "core_bits.h"
#include "core_complex.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_time.h"
#include "snd_buffer.h"

/**
 * Fast-Fourier-Transform.
 * More info: https://en.wikipedia.org/wiki/Fast_Fourier_transform
 * Pre-condition: bits_ispow2(count).
 * Pre-condition: count <= 8192.
 */
static void snd_fft(Complex buffer[], const u32 count) {
  diag_assert(bits_ispow2_32(count));

  /**
   * Basic (recursive) Cooley-Tukey FFT implementation.
   * More info: https://en.wikipedia.org/wiki/Cooley%E2%80%93Tukey_FFT_algorithm
   */

  if (count < 2) {
    return; // Recursion done.
  }

  // Split even and odd indices into their own buffers.
  const u32 countHalf  = count / 2;
  Complex*  bufferEven = alloc_array_t(g_allocScratch, Complex, countHalf);
  Complex*  bufferOdd  = alloc_array_t(g_allocScratch, Complex, countHalf);
  for (u32 i = 0; i != countHalf; ++i) {
    bufferEven[i] = buffer[i * 2];
    bufferOdd[i]  = buffer[i * 2 + 1];
  }

  // Process both halves.
  snd_fft(bufferEven, countHalf);
  snd_fft(bufferOdd, countHalf);

  // Compute the Discrete-Fourier-Transform.
  for (u32 i = 0; i != countHalf; ++i) {
    const Complex exp     = complex_exp(complex(0, -2.0 * math_pi_f64 * i / count));
    const Complex t       = complex_mul(exp, bufferOdd[i]);
    buffer[i]             = complex_add(bufferEven[i], t);
    buffer[countHalf + i] = complex_sub(bufferEven[i], t);
  }
}

void snd_buffer_clear(const SndBuffer buffer) {
  mem_set(mem_create(buffer.frames, sizeof(SndBufferFrame) * buffer.frameCount), 0);
}

f32* snd_buffer_samples(const SndBuffer buffer) { return (f32*)buffer.frames; }

SndBufferView snd_buffer_view(const SndBuffer buffer) {
  return (SndBufferView){
      .frames     = buffer.frames,
      .frameCount = buffer.frameCount,
      .frameRate  = buffer.frameRate,
  };
}

SndBufferView snd_buffer_slice(const SndBufferView view, const u32 offset, const u32 count) {
  diag_assert(view.frameCount >= offset + count);
  return (SndBufferView){
      .frames     = view.frames + offset,
      .frameCount = count,
      .frameRate  = view.frameRate,
  };
}

TimeDuration snd_buffer_duration(const SndBufferView view) {
  return view.frameCount * time_second / view.frameRate;
}

f32 snd_buffer_frequency_max(const SndBufferView view) {
  /**
   * https://en.wikipedia.org/wiki/Nyquist_frequency
   */
  return view.frameRate * 0.5f;
}

f32 snd_buffer_sample(const SndBufferView view, const SndChannel channel, const f32 frac) {
  diag_assert(frac >= 0.0 && frac <= 1.0f);
  diag_assert(view.frameCount >= 2);

  /**
   * Linear interpolation between the two closest samples.
   * NOTE: We can explore other methods that preserve the curve better, like Hermite interpolation.
   */

  const f32 index = frac * (view.frameCount - 1);
  const f32 edgeA = math_min(view.frameCount - 2, math_round_down_f32(index));
  const f32 edgeB = edgeA + 1.0f;
  const f32 valA  = view.frames[(usize)edgeA].samples[channel];
  const f32 valB  = view.frames[(usize)edgeB].samples[channel];
  return math_lerp(valA, valB, index - edgeA);
}

f32 snd_buffer_magnitude_peak(const SndBufferView view, const SndChannel channel) {
  f32 peak = 0;
  for (u32 frame = 0; frame != view.frameCount; ++frame) {
    const f32 sample    = view.frames[frame].samples[channel];
    const f32 sampleAbs = math_abs(sample);
    if (sampleAbs > peak) {
      peak = sampleAbs;
    }
  }
  return peak;
}

f32 snd_buffer_magnitude_rms(const SndBufferView view, const SndChannel channel) {
  f32 sum = 0;
  for (u32 frame = 0; frame != view.frameCount; ++frame) {
    const f32 sample = view.frames[frame].samples[channel];
    sum += sample * sample;
  }
  sum /= view.frameCount;
  return math_sqrt_f32(sum);
}

void snd_buffer_spectrum(const SndBufferView view, const SndChannel channel, f32 outMagnitudes[]) {
  diag_assert(bits_ispow2_32(view.frameCount));
  diag_assert(view.frameCount <= 8192);

  Complex* buffer = mem_stack(view.frameCount * sizeof(Complex)).ptr;

  // Initialize the fft buffer.
  for (u32 frame = 0; frame != view.frameCount; ++frame) {
    const f32 sample = view.frames[frame].samples[channel];
    buffer[frame]    = complex(sample, 0);
  }

  // Perform the fast-fourier-transform.
  snd_fft(buffer, view.frameCount);

  // Extract the output.
  // More info: http://howthefouriertransformworks.com/understanding-the-output-of-an-fft/
  const u32 outputCount = view.frameCount / 2;
  const f32 normFactor  = 1.0f / (f32)outputCount;
  for (u32 i = 0; i != outputCount; ++i) {
    const Complex val = buffer[i];
    // Use pythagoras to compute magnitude from the amplitudes of the cosine and sine waves.
    outMagnitudes[i] = (f32)math_sqrt_f64(val.real * val.real + val.imaginary * val.imaginary);

    // Normalize the magnitude.
    outMagnitudes[i] *= normFactor;
  }
}
