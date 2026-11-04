// OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
// Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#include "audio/AudioFx.h"

#include <algorithm>
#include <cmath>

namespace osf {

namespace {

constexpr double kPi = 3.14159265358979323846;

double BesselI0(double x)
{
    double sum = 1, term = 1;
    for (int k = 1; k < 30; ++k)
    {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
    }
    return sum;
}

// windowed-sinc resampler: Kaiser window (beta 8), 24 zero crossings a side, cutoff just under the lower Nyquist
std::vector<float> SincResample(const std::vector<float>& in, int srcRate, int dstRate)
{
    if (in.empty() || srcRate <= 0 || dstRate <= 0)
        return {};
    const double ratio = static_cast<double>(dstRate) / srcRate;
    const double cutoff = 0.92 * std::min(1.0, ratio);           // of the source Nyquist
    const int half = 24;
    // the Kaiser window (beta 8) as a table over |u| in [0, 1]
    static std::vector<double> win;
    if (win.empty())
    {
        win.resize(4097);
        const double i0b = BesselI0(8.0);
        for (size_t k = 0; k < win.size(); ++k)
        {
            const double u = k / 4096.0;
            win[k] = BesselI0(8.0 * std::sqrt(std::max(0.0, 1 - u * u))) / i0b;
        }
    }
    const size_t n = static_cast<size_t>(std::ceil(in.size() * ratio));
    std::vector<float> out(n);
    const double span = half / cutoff;                            // window half width in source samples
    for (size_t o = 0; o < n; ++o)
    {
        const double centre = o / ratio;
        const long lo = static_cast<long>(std::ceil(centre - span)), hi = static_cast<long>(std::floor(centre + span));
        double acc = 0;
        for (long s = std::max(0L, lo); s <= hi && s < static_cast<long>(in.size()); ++s)
        {
            const double t = s - centre;
            const double x = t * cutoff;
            const double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double u = std::fabs(t / span);
            const double w = u < 1 ? win[static_cast<size_t>(u * 4096)] : 0;
            acc += in[static_cast<size_t>(s)] * sinc * w * cutoff;
        }
        out[o] = static_cast<float>(acc);
    }
    return out;
}

// second-order high-pass (RBJ, Q 0.707)
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void HighPass(double f, double rate)
    {
        const double w = 2 * kPi * f / rate, c = std::cos(w), al = std::sin(w) / (2 * 0.7071);
        const double a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = (1 + c) / 2 / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    float Run(float x)
    {
        const double v = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = v;
        return static_cast<float>(v);
    }
};

} // namespace

std::vector<float> PlainEffect(const std::vector<int16_t>& pcm, int rate, int dstRate)
{
    std::vector<float> out;
    if (pcm.empty() || rate <= 0)
        return out;
    const size_t n = static_cast<size_t>(static_cast<double>(pcm.size()) * dstRate / rate);
    out.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double src = static_cast<double>(i) * rate / dstRate;
        const size_t a = std::min(pcm.size() - 1, static_cast<size_t>(src));
        const size_t b = std::min(pcm.size() - 1, a + 1);
        const double f = src - static_cast<double>(a);
        out[i] = static_cast<float>((pcm[a] * (1 - f) + pcm[b] * f) / 32768.0 * 0.75); // the earlier mix level
    }
    return out;
}

void LevelEffect(std::vector<float>& y, int dstRate)
{
    // loudness levelling: RMS of the loud part toward a common target, gain between 0.5 and 2.5, peak at most 0.9
    double sum = 0;
    size_t cnt = 0;
    float peak = 0;
    for (float v : y)
    {
        peak = std::max(peak, std::fabs(v));
        if (std::fabs(v) > 0.02f)
        {
            sum += static_cast<double>(v) * v;
            ++cnt;
        }
    }
    if (cnt > 0 && peak > 0)
    {
        const double rms = std::sqrt(sum / cnt);
        double g = std::clamp(0.18 / std::max(rms, 1e-6), 0.5, 2.5);
        g = std::min(g, 0.9 / peak);
        for (float& v : y)
            v = static_cast<float>(v * g);
    }
    // fades: 0.3 ms in (keeps the attack crisp), 3 ms out
    const size_t in = std::min(y.size() / 2, static_cast<size_t>(dstRate * 0.0003) + 1);
    const size_t out = std::min(y.size() / 2, static_cast<size_t>(dstRate * 0.003));
    for (size_t i = 0; i < in; ++i)
        y[i] *= static_cast<float>(i) / in;
    for (size_t i = 0; i < out; ++i)
        y[y.size() - 1 - i] *= static_cast<float>(i) / out;
}

std::vector<float> EnhanceEffect(const std::vector<int16_t>& pcm, int srcRate, int dstRate)
{
    if (pcm.empty() || srcRate <= 0)
        return {};
    std::vector<float> x(pcm.size());
    // DC removal: one-pole high-pass at about 20 Hz
    {
        const float r = static_cast<float>(1.0 - 2 * kPi * 20.0 / srcRate);
        float px = 0, py = 0;
        for (size_t i = 0; i < pcm.size(); ++i)
        {
            const float v = pcm[i] / 32768.0f;
            py = v - px + r * py;
            px = v;
            x[i] = py;
        }
    }
    // downward expander under the 8-bit floor (one step of 8-bit = 1/128): fast attack, slow release
    {
        const float thr = 0.012f, att = std::exp(-1.0f / (0.001f * srcRate)), rel = std::exp(-1.0f / (0.060f * srcRate));
        float env = 0;
        for (float& v : x)
        {
            const float a = std::fabs(v);
            env = a > env ? att * env + (1 - att) * a : rel * env + (1 - rel) * a;
            if (env < thr)
            {
                const float g = env / thr;
                v *= g * g;
            }
        }
    }
    std::vector<float> y = SincResample(x, srcRate, dstRate);
    // brightener: the 1.8 .. 5 kHz band driven through a soft curve makes overtones; only those above 5 kHz are kept
    // (two second-order high-passes) and added at a level that continues the sound's own spectrum: their energy is
    // set to 45 % of the band's, as in natural sounds, whose energy keeps falling gently above 5 kHz
    {
        Biquad h1, h2, o1, o2;
        h1.HighPass(1800, dstRate);
        h2.HighPass(1800, dstRate);
        o1.HighPass(5200, dstRate);
        o2.HighPass(5200, dstRate);
        std::vector<float> over(y.size());
        double bandE = 0, overE = 0, allE = 0;
        for (size_t i = 0; i < y.size(); ++i)
        {
            allE += static_cast<double>(y[i]) * y[i];
            const float band = h2.Run(h1.Run(y[i]));
            bandE += static_cast<double>(band) * band;
            over[i] = o2.Run(o1.Run(std::tanh(10.0f * band)));
            overE += static_cast<double>(over[i]) * over[i];
        }
        if (overE > 0)
        {
            // never more than 8 % of the whole sound's energy, so bright recordings (sheep) do not turn sharp
            const float g = static_cast<float>(std::min({ 4.0, std::sqrt(0.45 * bandE / overE), std::sqrt(0.08 * allE / overE) }));
            for (size_t i = 0; i < y.size(); ++i)
                y[i] += g * over[i];
        }
    }
    // presence: +4 dB above about 2.5 kHz (a high-passed copy added), against the dull top of 8-bit recordings
    {
        Biquad p1;
        p1.HighPass(2500, dstRate);
        for (float& v : y)
            v += 0.58f * p1.Run(v);
    }
    LevelEffect(y, dstRate);
    return y;
}

void Room::Init(int rate)
{
    // delay lengths in samples at 44.1 kHz (mutually prime), scaled to the device rate; the right channel uses the
    // second half of the combs
    static const int kComb[8] = { 1116, 1188, 1277, 1356, 1139, 1211, 1300, 1379 };
    static const int kAp[4] = { 556, 441, 579, 464 };
    const double s = rate / 44100.0;
    for (int i = 0; i < 8; ++i)
        _comb[static_cast<size_t>(i)].buf.assign(static_cast<size_t>(kComb[i] * s * 0.8) + 1, 0.0f); // a small room
    for (int i = 0; i < 4; ++i)
        _ap[static_cast<size_t>(i)].buf.assign(static_cast<size_t>(kAp[i] * s) + 1, 0.0f);
}

void Room::Process(float in, float& l, float& r)
{
    if (_comb[0].buf.empty())
        return;
    float out[2] = { 0, 0 };
    for (size_t c = 0; c < 8; ++c)
    {
        Comb& k = _comb[c];
        const float o = k.buf[k.i];
        k.store = o * (1 - _damp) + k.store * _damp;
        k.buf[k.i] = in + k.store * _feedback;
        k.i = (k.i + 1) % k.buf.size();
        out[c / 4] += o;
    }
    for (size_t a = 0; a < 4; ++a)
    {
        AllPass& p = _ap[a];
        float& v = out[a / 2];
        const float b = p.buf[p.i];
        p.buf[p.i] = v + b * 0.5f;
        v = b - v;
        p.i = (p.i + 1) % p.buf.size();
    }
    l += out[0] * 0.25f;
    r += out[1] * 0.25f;
}

} // namespace osf
