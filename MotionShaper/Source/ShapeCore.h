#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <random>

// Envelope shapes: a list of points (x = time 0..1 inside a segment, y = level 0..1).
// Each point's "c" bends the segment that STARTS at that point:
//   c > 0 : slow start, fast end      c < 0 : fast start, slow end
//
// A layer's pattern is up to 32 steps grouped into segments. One segment covers one or more
// steps and owns one shape, stretched across all of its steps.
namespace Shape
{
    constexpr int maxPoints = 16;
    constexpr int lutSize   = 512;
    constexpr int maxSteps  = 32;
    constexpr int numLayers = 6;
    constexpr int numPresets = 11;
    constexpr int presetRandom = 8;

    struct Point { float x = 0.0f; float y = 1.0f; float c = 0.0f; };
    using Points = std::vector<Point>;

    inline float curveShape (float u, float c)
    {
        if (c > 0.0f) return std::pow (u, 1.0f + 4.0f * c);
        if (c < 0.0f) return 1.0f - std::pow (1.0f - u, 1.0f - 4.0f * c);
        return u;
    }

    inline float evaluate (const Points& pts, float t)
    {
        if (pts.empty()) return 1.0f;
        if (t <= pts.front().x) return pts.front().y;
        if (t >= pts.back().x)  return pts.back().y;

        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            const Point& a = pts[i];
            const Point& b = pts[i + 1];
            if (t <= b.x)
            {
                const float span = b.x - a.x;
                const float u = span > 1.0e-6f ? (t - a.x) / span : 1.0f;
                return a.y + (b.y - a.y) * curveShape (u, a.c);
            }
        }
        return pts.back().y;
    }

    // Keeps a shape valid: sorted, in range, first point at x=0, last point at x=1
    inline void sanitise (Points& pts)
    {
        if (pts.size() < 2)
        {
            pts.clear();
            pts.push_back (Point { 0.0f, 1.0f, 0.0f });
            pts.push_back (Point { 1.0f, 1.0f, 0.0f });
            return;
        }
        std::stable_sort (pts.begin(), pts.end(), [] (const Point& a, const Point& b) { return a.x < b.x; });
        if ((int) pts.size() > maxPoints) pts.resize ((size_t) maxPoints);
        for (auto& p : pts)
        {
            p.x = std::clamp (p.x, 0.0f, 1.0f);
            p.y = std::clamp (p.y, 0.0f, 1.0f);
            p.c = std::clamp (p.c, -1.0f, 1.0f);
        }
        pts.front().x = 0.0f;
        pts.back().x  = 1.0f;
    }

    inline const char* presetName (int i)
    {
        static const char* names[numPresets] = { "Flat", "Pump", "Gate", "Ramp Up", "Ramp Down",
                                                 "Triangle", "Sine", "Triple", "Random", "Pluck", "Swell" };
        return names[std::clamp (i, 0, numPresets - 1)];
    }

    inline Points makePreset (int index)
    {
        Points p;
        auto add = [&p] (float x, float y, float c = 0.0f) { p.push_back (Point { x, y, c }); };

        switch (index)
        {
            case 1:  add (0.0f, 0.0f, -0.6f); add (1.0f, 1.0f); break;                          // sidechain pump
            case 2:  add (0.0f, 1.0f); add (0.48f, 1.0f); add (0.5f, 0.0f); add (1.0f, 0.0f); break; // gate
            case 3:  add (0.0f, 0.0f); add (1.0f, 1.0f); break;
            case 4:  add (0.0f, 1.0f); add (1.0f, 0.0f); break;
            case 5:  add (0.0f, 0.0f); add (0.5f, 1.0f); add (1.0f, 0.0f); break;
            case 6:
                for (int k = 0; k <= 8; ++k)
                    add ((float) k / 8.0f, 0.5f - 0.5f * std::cos (6.2831853f * (float) k / 8.0f));
                break;
            case 7:                                                                             // three quick pulses
                for (int k = 0; k < 3; ++k)
                {
                    const float x0 = (float) k / 3.0f;
                    add (x0, 1.0f); add (x0 + 0.18f, 1.0f); add (x0 + 0.22f, 0.0f);
                }
                add (1.0f, 0.0f);
                break;
            case presetRandom:
            {
                static std::mt19937 rng { std::random_device{}() };
                std::uniform_real_distribution<float> uni (0.0f, 1.0f);
                const int inner = 3 + (int) (rng() % 4);
                std::vector<float> xs;
                for (int k = 0; k < inner; ++k) xs.push_back (0.08f + 0.84f * uni (rng));
                std::sort (xs.begin(), xs.end());
                add (0.0f, uni (rng), 2.0f * uni (rng) - 1.0f);
                for (float x : xs) add (x, uni (rng), 2.0f * uni (rng) - 1.0f);
                add (1.0f, uni (rng));
                break;
            }
            case 9:                                                                             // sharp hit, long decay
                add (0.0f, 0.3f); add (0.004f, 0.96f, -0.55f); add (0.5f, 0.38f, -0.5f); add (1.0f, 0.3f);
                break;
            case 10:                                                                            // rise, then hold
                add (0.0f, 0.1f, 0.3f); add (0.55f, 0.75f, -0.4f); add (1.0f, 0.8f);
                break;
            default: add (0.0f, 1.0f); add (1.0f, 1.0f); break;                                 // flat
        }
        sanitise (p);
        return p;
    }

    // ------------------------------------------------------------------ patterns
    struct Pattern
    {
        std::array<Points, maxSteps> pts;       // only the first step of each segment is used
        std::array<bool, maxSteps> start;       // true = this step begins a new segment
        std::array<bool, maxSteps> muted;       // true = this step has no effect

        Pattern()
        {
            start.fill (true);
            muted.fill (false);
            for (auto& p : pts) p = makePreset (0);
        }
    };

    inline int segmentHead (const Pattern& p, int step)
    {
        step = std::clamp (step, 0, maxSteps - 1);
        while (step > 0 && ! p.start[(size_t) step]) --step;
        return step;
    }

    inline int segmentLength (const Pattern& p, int head, int numSteps)
    {
        int e = head + 1;
        while (e < numSteps && ! p.start[(size_t) e]) ++e;
        return std::max (1, e - head);
    }

    // Moves the last step of the segment starting at "head". A segment that gets partly covered
    // keeps its shape on what is left of it. Freed steps become one segment with the same shape.
    inline void setSegmentEnd (Pattern& p, int head, int newEnd, int numSteps)
    {
        newEnd = std::clamp (newEnd, head, numSteps - 1);
        const int oldEnd = head + segmentLength (p, head, numSteps) - 1;
        if (newEnd == oldEnd) return;

        if (newEnd > oldEnd)
        {
            const int after = newEnd + 1;
            if (after < maxSteps && ! p.start[(size_t) after])
                p.pts[(size_t) after] = p.pts[(size_t) segmentHead (p, after)];
            for (int i = oldEnd + 1; i <= newEnd; ++i) p.start[(size_t) i] = false;
            if (after < maxSteps) p.start[(size_t) after] = true;
        }
        else
        {
            p.start[(size_t) newEnd + 1] = true;
            p.pts[(size_t) newEnd + 1] = p.pts[(size_t) head];
        }
    }

    // Random segment lengths and shapes across the first numSteps steps
    inline void randomisePattern (Pattern& p, int numSteps)
    {
        static std::mt19937 rng { std::random_device{}() };
        static const int lengths[7] = { 1, 1, 1, 2, 2, 3, 4 };
        static const int shapes[10] = { 1, 2, 3, 4, 5, 6, 7, presetRandom, 9, 10 };

        p.start.fill (true);
        p.muted.fill (false);
        numSteps = std::clamp (numSteps, 1, maxSteps);
        for (int s = 0; s < numSteps; )
        {
            const int len = std::min (lengths[rng() % 7], numSteps - s);
            p.pts[(size_t) s] = makePreset (shapes[rng() % 10]);
            for (int i = s + 1; i < s + len; ++i) p.start[(size_t) i] = false;
            s += len;
        }
    }

    // ------------------------------------------------------------------ timing
    struct StepPos { int step = 0; float t = 0.0f; };

    // pos = position in steps, swing 0..0.75: every first step of a pair lasts (1 + swing), the second (1 - swing).
    inline StepPos stepFromPosition (double pos, int numSteps, double swing)
    {
        numSteps = std::max (1, numSteps);
        double cyc = std::fmod (pos, (double) numSteps);
        if (cyc < 0.0) cyc += (double) numSteps;

        const int pair = (int) (cyc * 0.5);
        const double p = cyc - 2.0 * (double) pair;
        StepPos out;

        if (swing <= 0.0 || 2 * pair + 1 >= numSteps)      // no swing, or an unpaired last step
        {
            out.step = (int) cyc;
            out.t = (float) (cyc - (double) out.step);
        }
        else if (p < 1.0 + swing)
        {
            out.step = 2 * pair;
            out.t = (float) (p / (1.0 + swing));
        }
        else
        {
            out.step = 2 * pair + 1;
            out.t = (float) ((p - 1.0 - swing) / (1.0 - swing));
        }
        out.step = std::clamp (out.step, 0, numSteps - 1);
        out.t = std::clamp (out.t, 0.0f, 1.0f);
        return out;
    }
}
