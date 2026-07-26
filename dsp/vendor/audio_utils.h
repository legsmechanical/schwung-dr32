/* audio_utils.h — small DSP math helpers for the Echidna engine.
 *
 * All free functions, inline, namespace ech. No allocation, RT-safe. */
#pragma once
#include <cmath>

namespace ech {

/* Equal-tempered MIDI note -> frequency in Hz (A4 = note 69 = 440 Hz). */
inline float midiToHz(float midiNote) {
    return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
}

/* Fast rational approximation of tanh via the Pade [7/6] series, accurate to
 * ~2e-5 over [-3,3] and clamped to +/-1 beyond (the rational form's poles sit
 * outside the clamp). Cheaper than std::tanh and denormal-free. */
inline float fastTanh(float x) {
    if (x < -4.97f) return -1.0f;
    if (x >  4.97f) return  1.0f;
    float x2 = x * x;
    return x * (10395.0f + x2 * (1260.0f + x2 * 21.0f)) /
           (10395.0f + x2 * (4725.0f + x2 * (210.0f + x2)));
}

/* Flush subnormal / tiny magnitudes to exactly zero to avoid denormal CPU
 * penalties in feedback paths. */
inline float flushDenorm(float x) {
    return (std::fabs(x) < 1e-15f) ? 0.0f : x;
}

} // namespace ech
