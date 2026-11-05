#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <xmmintrin.h>

// The 3D headphone sound, in pure C++ (no JUCE).
//   Table      measured directions (Assets/hrtf_kemar.bin), resampled to the session rate, shared by all tracks
//   Voice      renders one mono source from one direction, blending the nearest measured directions
//   EarlyRoom  first reflections from the six walls of a shared room
namespace Hrtf
{
    class Table
    {
    public:
        static std::shared_ptr<const Table> get (const void* data, size_t size, double sampleRate)
        {
            static std::mutex lock;
            static std::map<int, std::weak_ptr<const Table>> cache;

            const std::lock_guard<std::mutex> guard (lock);
            const int key = (int) std::round (sampleRate);
            if (auto existing = cache[key].lock())
                return existing;

            auto fresh = std::shared_ptr<const Table> (new Table (data, size, sampleRate));
            cache[key] = fresh;
            return fresh;
        }

        int numDirs = 0, taps = 0, lead = 0;
        float maxItd = 0.0f;
        std::vector<float> dirs;       // x, y, z, itd for each direction
        std::vector<float> coefs;      // left[taps], right[taps] for each direction

        const float* coef (int dir, int ear) const { return coefs.data() + ((size_t) dir * 2 + (size_t) ear) * (size_t) taps; }

    private:
        // Windowed-sinc interpolation, which keeps the highs when moving to another sample rate.
        // At the file's own rate it returns the original samples unchanged.
        static double interpolate (const float* src, int count, double pos)
        {
            constexpr int halfWidth = 8;
            constexpr double pi = 3.14159265358979;
            const int centre = (int) std::floor (pos);
            double sum = 0.0;
            for (int j = std::max (0, centre - halfWidth + 1); j <= std::min (count - 1, centre + halfWidth); ++j)
            {
                const double t = pos - (double) j;
                const double sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (pi * t) / (pi * t);
                const double window = 0.5 + 0.5 * std::cos (pi * t / (double) halfWidth);
                sum += (double) src[j] * sinc * window;
            }
            return sum;
        }

        Table (const void* data, size_t size, double sampleRate)
        {
            const auto* p = static_cast<const char*> (data);
            int32_t n = 0, srcTaps = 0;
            float srcRate = 0.0f;
            if (size < 16 || std::memcmp (p, "HRTF", 4) != 0) return;
            std::memcpy (&n, p + 4, 4);
            std::memcpy (&srcTaps, p + 8, 4);
            std::memcpy (&srcRate, p + 12, 4);
            const size_t record = (size_t) (4 + 2 * srcTaps) * sizeof (float);
            if (n <= 0 || srcTaps <= 0 || srcRate <= 0.0f || size < 16 + record * (size_t) n) return;

            // Stretch the responses to the session rate, rounded up to a multiple of 4 for SSE, at most 256 taps
            // Interpolating an abrupt start also rings just before it. That part must be kept or the tone changes,
            // so every response starts "lead" samples early. Voice delays its dry signal to match.
            const double ratio = (double) srcRate / sampleRate;
            const bool sameRate = std::abs (ratio - 1.0) < 1.0e-9;
            lead = sameRate ? 0 : (int) std::ceil (8.0 / ratio);
            const int wanted = std::min (256, (int) std::ceil ((double) srcTaps / ratio) + lead);
            taps = (wanted + 3) & ~3;
            numDirs = n;
            dirs.resize ((size_t) n * 4);
            coefs.resize ((size_t) n * 2 * (size_t) taps);

            std::vector<float> src ((size_t) srcTaps);
            const char* in = p + 16;
            for (int d = 0; d < n; ++d, in += record)
            {
                std::memcpy (&dirs[(size_t) d * 4], in, 4 * sizeof (float));
                dirs[(size_t) d * 4 + 3] = (float) (dirs[(size_t) d * 4 + 3] / ratio);
                maxItd = std::max (maxItd, std::abs (dirs[(size_t) d * 4 + 3]));

                for (int ear = 0; ear < 2; ++ear)
                {
                    std::memcpy (src.data(), in + (4 + (size_t) ear * (size_t) srcTaps) * sizeof (float), (size_t) srcTaps * sizeof (float));
                    float* out = coefs.data() + ((size_t) d * 2 + (size_t) ear) * (size_t) taps;
                    for (int i = 0; i < taps; ++i)
                        out[i] = (float) (interpolate (src.data(), srcTaps, (double) (i - lead) * ratio) * ratio);
                }
            }
        }
    };

    class Voice
    {
    public:
        void prepare (std::shared_ptr<const Table> newTable)
        {
            table = std::move (newTable);
            if (table == nullptr || table->taps == 0) { table = nullptr; return; }

            const auto taps = (size_t) table->taps;
            for (int ear = 0; ear < 2; ++ear)
            {
                cur[ear].assign (taps, 0.0f);
                target[ear].assign (taps, 0.0f);
                hist[ear].assign (taps * 2, 0.0f);
            }
            delaySize = 8;
            while (delaySize < (int) table->maxItd + table->lead + 8) delaySize *= 2;
            delay.assign ((size_t) delaySize, 0.0f);
            reset();
            ready = false;
        }

        void reset()
        {
            for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
            std::fill (delay.begin(), delay.end(), 0.0f);
            histPos = 0;
            delayPos = 0;
            counter = 0;
        }

        // ux, uy, uz: unit vector (right, front, up). Blends the three nearest measured directions.
        void setDirection (float ux, float uy, float uz)
        {
            if (table == nullptr) return;
            if (ready && ux == lastX && uy == lastY && uz == lastZ) return;
            lastX = ux; lastY = uy; lastZ = uz;

            int best[3] = { 0, 0, 0 };
            float score[3] = { -2.0f, -2.0f, -2.0f };
            const float* d = table->dirs.data();
            for (int i = 0; i < table->numDirs; ++i, d += 4)
            {
                const float dot = d[0] * ux + d[1] * uy + d[2] * uz;
                if (dot > score[0])      { score[2] = score[1]; best[2] = best[1]; score[1] = score[0]; best[1] = best[0]; score[0] = dot; best[0] = i; }
                else if (dot > score[1]) { score[2] = score[1]; best[2] = best[1]; score[1] = dot; best[1] = i; }
                else if (dot > score[2]) { score[2] = dot; best[2] = i; }
            }

            float weight[3], total = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float dist = 1.0f - std::min (1.0f, score[k]);
                weight[k] = 1.0f / (dist + 1.0e-5f);
                total += weight[k];
            }

            targetItd = 0.0f;
            const int taps = table->taps;
            for (int ear = 0; ear < 2; ++ear)
                std::fill (target[ear].begin(), target[ear].end(), 0.0f);
            for (int k = 0; k < 3; ++k)
            {
                const float w = weight[k] / total;
                targetItd += w * table->dirs[(size_t) best[k] * 4 + 3];
                for (int ear = 0; ear < 2; ++ear)
                {
                    const float* c = table->coef (best[k], ear);
                    float* t = target[ear].data();
                    for (int i = 0; i < taps; ++i) t[i] += w * c[i];
                }
            }

            if (! ready)
            {
                for (int ear = 0; ear < 2; ++ear) cur[ear] = target[ear];
                itd = targetItd;
                ready = true;
            }
            morphing = true;
        }

        // dry: the input, delayed to line up with the outputs
        void process (float in, float& outL, float& outR, float& dry)
        {
            if (table == nullptr) { outL = outR = dry = in; return; }

            if (morphing && ++counter >= 32)
            {
                counter = 0;
                morph();
            }
            itd += 0.002f * (targetItd - itd);

            delay[(size_t) delayPos] = in;
            const float left  = readDelay (std::max (0.0f, itd));      // positive = source on the right, left ear later
            const float right = readDelay (std::max (0.0f, -itd));
            dry = readDelay ((float) table->lead);
            delayPos = (delayPos + 1) & (delaySize - 1);

            const int taps = table->taps;
            histPos = histPos == 0 ? taps - 1 : histPos - 1;
            hist[0][(size_t) histPos] = hist[0][(size_t) histPos + (size_t) taps] = left;
            hist[1][(size_t) histPos] = hist[1][(size_t) histPos + (size_t) taps] = right;
            outL = dot (cur[0].data(), hist[0].data() + histPos, taps);
            outR = dot (cur[1].data(), hist[1].data() + histPos, taps);
        }

    private:
        float readDelay (float d) const
        {
            float r = (float) delayPos - d;
            if (r < 0.0f) r += (float) delaySize;
            const int i0 = (int) r & (delaySize - 1);
            const int i1 = (i0 + 1) & (delaySize - 1);
            const float f = r - std::floor (r);
            return delay[(size_t) i0] + (delay[(size_t) i1] - delay[(size_t) i0]) * f;
        }

        void morph()
        {
            float change = 0.0f;
            for (int ear = 0; ear < 2; ++ear)
            {
                float* c = cur[ear].data();
                const float* t = target[ear].data();
                for (int i = 0; i < table->taps; ++i)
                {
                    const float step = t[i] - c[i];
                    c[i] += 0.25f * step;
                    change = std::max (change, std::abs (step));
                }
            }
            if (change < 1.0e-5f) morphing = false;
        }

        static float dot (const float* a, const float* b, int n)
        {
            __m128 sum = _mm_setzero_ps();
            for (int i = 0; i < n; i += 4)
                sum = _mm_add_ps (sum, _mm_mul_ps (_mm_loadu_ps (a + i), _mm_loadu_ps (b + i)));
            float v[4];
            _mm_storeu_ps (v, sum);
            return v[0] + v[1] + v[2] + v[3];
        }

        std::shared_ptr<const Table> table;
        std::vector<float> cur[2], target[2], hist[2], delay;
        int histPos = 0, delayPos = 0, delaySize = 8, counter = 0;
        float itd = 0.0f, targetItd = 0.0f;
        float lastX = 0.0f, lastY = 0.0f, lastZ = 0.0f;
        bool ready = false, morphing = false;
    };

    // The six first reflections of a room with the listener in the middle
    class EarlyRoom
    {
    public:
        void prepare (double sampleRate)
        {
            sr = (float) sampleRate;
            size = 2;
            while (size < (int) (sampleRate * 0.16)) size *= 2;
            line.assign ((size_t) size, 0.0f);
            damp = 1.0f - std::exp (-6.2831853f * 6000.0f / sr);
            reset();
            ready = false;
        }

        void reset()
        {
            std::fill (line.begin(), line.end(), 0.0f);
            pos = 0;
            lp = 0.0f;
            counter = 0;
        }

        // x, y, z: source position -1..1 (right, front, up). roomSize 0..1
        void setGeometry (float x, float y, float z, float roomSize)
        {
            const float halfW = 2.0f + 8.0f * roomSize;
            const float halfH = 1.5f + 3.0f * roomSize;
            const float sx = x * halfW * 0.8f, sy = y * halfW * 0.8f, sz = z * halfH * 0.8f;
            const float direct = std::sqrt (sx * sx + sy * sy + sz * sz);

            const float images[6][3] = { {  2.0f * halfW - sx, sy, sz }, { -2.0f * halfW - sx, sy, sz },
                                         { sx,  2.0f * halfW - sy, sz }, { sx, -2.0f * halfW - sy, sz },
                                         { sx, sy,  2.0f * halfH - sz }, { sx, sy, -2.0f * halfH - sz } };
            for (int r = 0; r < 6; ++r)
            {
                const auto* im = images[r];
                const float dist = std::max (0.1f, std::sqrt (im[0] * im[0] + im[1] * im[1] + im[2] * im[2]));
                const float ux = im[0] / dist;

                auto& t = refl[r];
                const float base = std::max (0.001f, (dist - direct) / 343.0f) * sr;
                t.targetDelay[0] = base + std::max (0.0f,  ux) * 0.00065f * sr;
                t.targetDelay[1] = base + std::max (0.0f, -ux) * 0.00065f * sr;

                // Louder at the nearer ear, and quieter with the extra distance travelled
                const float level = 0.75f * (1.0f + direct) / (1.0f + dist);
                const float angle = (ux + 1.0f) * 0.7853982f;
                t.targetGain[0] = level * std::cos (angle) * 1.4142136f * (im[1] < 0.0f ? 0.8f : 1.0f);
                t.targetGain[1] = level * std::sin (angle) * 1.4142136f * (im[1] < 0.0f ? 0.8f : 1.0f);
            }

            if (! ready)
            {
                for (auto& t : refl)
                    for (int ear = 0; ear < 2; ++ear) { t.delay[ear] = t.targetDelay[ear]; t.gain[ear] = t.targetGain[ear]; }
                ready = true;
            }
        }

        void process (float in, float& outL, float& outR)
        {
            lp += damp * (in - lp);
            line[(size_t) pos] = lp;

            if (++counter >= 32)
            {
                counter = 0;
                for (auto& t : refl)
                    for (int ear = 0; ear < 2; ++ear)
                    {
                        t.delay[ear] += 0.05f * (t.targetDelay[ear] - t.delay[ear]);
                        t.gain[ear]  += 0.05f * (t.targetGain[ear]  - t.gain[ear]);
                    }
            }

            float l = 0.0f, r = 0.0f;
            for (const auto& t : refl)
            {
                l += read (t.delay[0]) * t.gain[0];
                r += read (t.delay[1]) * t.gain[1];
            }
            pos = (pos + 1) & (size - 1);
            outL = l;
            outR = r;
        }

    private:
        float read (float d) const
        {
            float rp = (float) pos - d;
            if (rp < 0.0f) rp += (float) size;
            const int i0 = (int) rp & (size - 1);
            const int i1 = (i0 + 1) & (size - 1);
            const float f = rp - std::floor (rp);
            return line[(size_t) i0] + (line[(size_t) i1] - line[(size_t) i0]) * f;
        }

        struct Reflection { float delay[2] {}, targetDelay[2] {}, gain[2] {}, targetGain[2] {}; };

        Reflection refl[6];
        std::vector<float> line;
        float sr = 44100.0f, damp = 0.5f, lp = 0.0f;
        int size = 2, pos = 0, counter = 0;
        bool ready = false;
    };
}
