// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace osf {

// The engine's "modern sound" processing (an engine addition, Options > Advanced). It works on the samples the
// engine has just decoded from the player's own sound files, in memory only, while the game runs: nothing is written
// back or stored, and nothing of the original is part of OpenSimFarm.

// Prepares one decoded 8-bit effect for playback at the device rate:
// * DC offset removal;
// * a gentle downward expander under the 8-bit noise floor (takes the hiss out of the quiet tails);
// * band-limited (windowed-sinc) resampling instead of straight-line interpolation, so the 11 kHz recording gains
//   no metallic images when it is played at 48 kHz;
// * a brightener (harmonic exciter): new overtones above the recording's 5 kHz limit, made from its own upper band,
//   so it sounds less muffled;
// * loudness levelling, so all effects sit at a similar level;
// * short fades at both ends against clicks.
std::vector<float> EnhanceEffect(const std::vector<int16_t>& pcm, int srcRate, int dstRate);

// Brings an effect to the common loudness (RMS of its loud part, gain 0.5 .. 2.5, peak at most 0.9) with short fades (0.3 ms in, 3 ms out).
void LevelEffect(std::vector<float>& y, int rate);

// The original path: straight-line resampling, as the engine played the effects before (Modern Sound off).
std::vector<float> PlainEffect(const std::vector<int16_t>& pcm, int srcRate, int dstRate);

// A small stereo room (Schroeder / Freeverb style: parallel damped combs, then all-pass diffusers).
class Room
{
public:
    void Init(int rate);
    // in: mono send; adds the wet stereo signal to l / r
    void Process(float in, float& l, float& r);

private:
    struct Comb { std::vector<float> buf; size_t i = 0; float store = 0; };
    struct AllPass { std::vector<float> buf; size_t i = 0; };
    std::array<Comb, 8> _comb;
    std::array<AllPass, 4> _ap;
    float _feedback = 0.80f, _damp = 0.30f;
};

// Soft limiter for the final mix: unchanged below 0.75, smoothly bent above, never past 1.
inline float SoftLimit(float x)
{
    const float a = x < 0 ? -x : x;
    if (a <= 0.75f)
        return x;
    const float over = (a - 0.75f) / 0.25f;
    const float y = 0.75f + 0.25f * (over / (1.0f + over));
    return x < 0 ? -y : y;
}

} // namespace osf
