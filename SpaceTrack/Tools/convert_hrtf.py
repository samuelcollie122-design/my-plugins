#!/usr/bin/env python3
"""Converts a SOFA HRTF file into SpaceTrack's Assets/hrtf_kemar.bin.

Only needed to rebuild the data. The plugin build uses the .bin file and does not need Python.
Needs: pip install h5py numpy
Usage: python convert_hrtf.py mit_kemar_normal_pinna.sofa ../Assets/hrtf_kemar.bin

What it does to every measured direction:
  1. Finds how much earlier the sound reaches one ear (the interaural time difference).
  2. Divides out the average of all directions (diffuse-field equalisation), so 3D mode has no overall colour.
  3. Keeps the bass flat, because a dummy head has almost no direction cues there and the loudspeaker used
     for the measurements could not reproduce it well.
  4. Rebuilds each ear's response as minimum phase, so it is short and directions can be blended without combing.

File format, little endian:
  char[4] "HRTF", int32 directions, int32 taps, float32 sampleRate, then for each direction:
  float32 x (right), y (front), z (up), itd (samples at sampleRate, positive = source on the right),
  float32 left[taps], float32 right[taps]
"""
import struct
import sys

import h5py
import numpy as np

TAPS = 96
FFT = 1024
UPSAMPLE = 8


def onset(h):
    """Time of the first arrival in samples, found on an upsampled copy."""
    n = len(h)
    spec = np.fft.rfft(h)
    fine = np.fft.irfft(spec, n * UPSAMPLE) * UPSAMPLE
    env = np.abs(fine)
    return float(np.argmax(env >= 0.2 * env.max())) / UPSAMPLE


def minimum_phase(mag):
    """Minimum-phase impulse response with the given FFT magnitude (real cepstrum method)."""
    n = (len(mag) - 1) * 2
    cep = np.fft.irfft(np.log(np.maximum(mag, 1.0e-6)), n)
    fold = np.zeros(n)
    fold[0] = cep[0]
    fold[1:n // 2] = 2.0 * cep[1:n // 2]
    fold[n // 2] = cep[n // 2]
    return np.fft.irfft(np.exp(np.fft.rfft(fold)), n)


def main(src, dst):
    f = h5py.File(src, "r")
    ir = f["Data.IR"][()]                 # directions, ears (0 = left, +y receiver), samples
    pos = f["SourcePosition"][()]         # azimuth (0 = front, 90 = left), elevation, distance
    sr = float(f["Data.SamplingRate"][()][0])
    assert f["ReceiverPosition"][()].reshape(-1)[1] > 0, "expected receiver 0 to be the left ear"

    az = np.radians(pos[:, 0])
    el = np.radians(pos[:, 1])
    x = -np.sin(az) * np.cos(el)          # right
    y = np.cos(az) * np.cos(el)           # front
    z = np.sin(el)                        # up

    itd = np.array([onset(ir[d, 0]) - onset(ir[d, 1]) for d in range(len(ir))])

    mags = np.abs(np.fft.rfft(ir, FFT, axis=2))
    freqs = np.fft.rfftfreq(FFT, 1.0 / sr)

    # Average power over the sphere. Rings near the top have fewer points, so weight each by the area it covers.
    rings, counts = np.unique(np.round(pos[:, 1], 1), return_counts=True)
    ring_count = dict(zip(rings, counts))
    weights = np.array([np.cos(np.radians(e)) / ring_count[round(e, 1)] for e in pos[:, 1]])
    weights = np.maximum(weights, 0.002)
    weights /= weights.sum()
    diffuse = np.sqrt(np.sum((mags ** 2) * weights[:, None, None], axis=(0, 1)) / 2.0)

    # Smooth the average so it removes colour, not the direction-dependent notches
    kernel = np.hanning(9)
    kernel /= kernel.sum()
    diffuse = np.convolve(np.pad(diffuse, 4, mode="edge"), kernel, mode="valid")

    gain = np.clip(mags / np.maximum(diffuse, 1.0e-6), 0.03, 4.0)

    # Flat bass, fading in the measured response between 150 and 400 Hz
    blend = np.clip((np.log(np.maximum(freqs, 1.0)) - np.log(150.0)) / (np.log(400.0) - np.log(150.0)), 0.0, 1.0)
    blend = 0.5 - 0.5 * np.cos(np.pi * blend)
    gain = np.exp(np.log(gain) * blend[None, None, :])

    # Level: straight ahead should match the stereo path's level in the band where hearing is most sensitive
    front = int(np.argmin(np.abs(pos[:, 0]) + np.abs(pos[:, 1])))
    band = (freqs >= 200.0) & (freqs <= 8000.0)
    gain /= np.sqrt(np.mean(gain[front, :, band] ** 2))

    fade = np.ones(TAPS)
    fade[-16:] = np.cos(np.linspace(0.0, np.pi / 2.0, 16)) ** 2

    out = np.zeros((len(ir), 2, TAPS), dtype=np.float32)
    for d in range(len(ir)):
        for ear in range(2):
            out[d, ear] = (minimum_phase(gain[d, ear]) [:TAPS] * fade).astype(np.float32)

    with open(dst, "wb") as o:
        o.write(b"HRTF")
        o.write(struct.pack("<iif", len(ir), TAPS, sr))
        for d in range(len(ir)):
            o.write(struct.pack("<ffff", x[d], y[d], z[d], itd[d]))
            o.write(out[d, 0].tobytes())
            o.write(out[d, 1].tobytes())

    # ------------------------------------------------------------- sanity report
    def direction(a, e):
        target = np.array([-np.sin(np.radians(a)) * np.cos(np.radians(e)), np.cos(np.radians(a)) * np.cos(np.radians(e)), np.sin(np.radians(e))])
        return int(np.argmax(x * target[0] + y * target[1] + z * target[2]))

    def band_db(d, ear, lo, hi):
        spec = np.abs(np.fft.rfft(out[d, ear], FFT))
        return 20.0 * np.log10(np.sqrt(np.mean(spec[(freqs >= lo) & (freqs < hi)] ** 2)) + 1.0e-9)

    print(f"directions {len(ir)}, taps {TAPS}, rate {sr:.0f}, largest value {np.abs(out).max():.3f}")
    print(f"itd range {itd.min():.1f} to {itd.max():.1f} samples ({1e6 * itd.max() / sr:.0f} us)")
    for name, a, e in (("front", 0, 0), ("right", 270, 0), ("left", 90, 0), ("back", 180, 0), ("up", 0, 90), ("down", 0, -40)):
        d = direction(a, e)
        print(f"{name:6s} xyz=({x[d]:+.2f},{y[d]:+.2f},{z[d]:+.2f}) itd={itd[d]:+6.1f}  "
              + "  ".join(f"{lo}-{hi}Hz L{band_db(d, 0, lo, hi):+5.1f} R{band_db(d, 1, lo, hi):+5.1f}"
                          for lo, hi in ((200, 1000), (2000, 5000), (6000, 10000), (10000, 16000))))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
