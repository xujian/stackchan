"""Synthesizes the nap sound effects (yawn, snore, wake) as 16 kHz mono WAVs.

Usage: python synth_nap_sounds.py OUT_DIR  (needs numpy + scipy)
Then for each WAV: ffmpeg -i x.wav -c:a libopus -b:a 24k -ar 16000 -ac 1 -frame_duration 60 -application audio x.ogg
Everything is procedural (no samples), so there is no licensing question.
"""
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt

SR = 16000
OUT = sys.argv[1]
rng = np.random.default_rng(7)


def t_axis(sec):
    return np.arange(int(sec * SR)) / SR


def bandpass(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo, hi], btype="band", fs=SR, output="sos"), x)


def lowpass(x, f, order=2):
    return sosfilt(butter(order, f, btype="low", fs=SR, output="sos"), x)


def glottal(f0):
    """Soft voiced source following a per-sample pitch curve: a sawtooth with its harsh top end rolled off."""
    phase = np.cumsum(f0 / SR) % 1.0
    return lowpass(2 * phase - 1, 2500, order=1)


def vowel(src, formants):
    """Very small formant filter: sum of band-passes around each (centre, width, gain)."""
    return sum(g * bandpass(src, c - w / 2, c + w / 2) for c, w, g in formants)


def env(n, attack, release, shape=1.5):
    """Smooth rise/fall envelope over n samples, attack/release as fractions of the length."""
    e = np.ones(n)
    a, r = int(n * attack), int(n * release)
    e[:a] = np.sin(np.linspace(0, np.pi / 2, a)) ** shape
    e[n - r:] = np.cos(np.linspace(0, np.pi / 2, r)) ** shape
    return e


def norm(x, peak_db):
    return x / np.max(np.abs(x)) * 10 ** (peak_db / 20)


def save(name, x):
    pad = np.zeros(int(0.05 * SR))
    wavfile.write(f"{OUT}/{name}.wav", SR, (np.concatenate([pad, x, pad]) * 32767).astype(np.int16))


def tone(f0, harmonics):
    """Clean cartoon voice: a few sine harmonics following a per-sample pitch curve."""
    phase = 2 * np.pi * np.cumsum(f0 / SR)
    return sum(a * np.sin(k * phase) for k, a in enumerate(harmonics, start=1))


def yawn():
    """Cartoon 'a~a~ah~' yawn: high, rises, wobbles, slides down to a sleepy low. ~1.5 s."""
    t = t_axis(1.5)
    p = t / t[-1]
    f0 = 520 + 260 * np.sin(np.pi * np.clip(p * 1.6, 0, 1)) - 220 * np.clip((p - 0.55) / 0.45, 0, 1)
    f0 *= 1 + 0.035 * np.sin(2 * np.pi * 7 * t) * np.clip((p - 0.3) / 0.3, 0, 1)   # vibrato kicks in on the 'ah~'
    voice = tone(f0, [1.0, 0.45, 0.25, 0.1, 0.05])
    voice = vowel(voice, [(900, 700, 1.0), (1600, 800, 0.6), (3000, 1200, 0.2)])
    air = bandpass(rng.standard_normal(len(t)), 1500, 5000) * 0.08 * (1 - p)
    x = (voice + air) * env(len(t), 0.12, 0.4)
    return norm(x, -9)


def snore():
    """Cartoon snore: buzzy 'hnnnk' going up on the inhale, then a whistling 'fweee~' coming down. ~1.9 s."""
    # Inhale: kazoo-like buzz, pitch climbing, with a gentle 18 Hz rattle
    ti = t_axis(0.9)
    pi_ = ti / ti[-1]
    f0 = 140 + 90 * pi_
    buzz = np.sign(np.sin(2 * np.pi * np.cumsum(f0 / SR)))
    buzz = vowel(buzz, [(700, 500, 1.0), (1400, 600, 0.4)])
    buzz *= 0.65 + 0.35 * np.sin(2 * np.pi * 18 * ti)
    inhale = buzz * env(len(ti), 0.4, 0.15)

    gap = np.zeros(int(0.15 * SR))

    # Exhale: soft whistle sliding down, a little breath under it
    te = t_axis(0.85)
    pe = te / te[-1]
    fw = 1500 - 650 * pe ** 0.8
    fw *= 1 + 0.01 * np.sin(2 * np.pi * 6 * te)
    whistle = tone(fw, [1.0, 0.08]) * 0.55
    breath = bandpass(rng.standard_normal(len(te)), 800, 3000) * 0.15
    exhale = (whistle + breath) * env(len(te), 0.12, 0.55)

    return norm(np.concatenate([inhale, gap, exhale]), -14)


def wake():
    """Bright two-note 'mm-hm?' chirp going up, plus a tiny sparkle at the end. ~0.7 s."""
    def note(sec, f_from, f_to):
        t = t_axis(sec)
        p = t / t[-1]
        f0 = f_from + (f_to - f_from) * (1 - (1 - p) ** 2)       # ease-out glide
        v = tone(f0, [1.0, 0.3, 0.12])
        return v * env(len(t), 0.15, 0.4)

    first = note(0.2, 600, 700)
    gap = np.zeros(int(0.05 * SR))
    second = note(0.32, 750, 1150)
    sparkle_t = t_axis(0.18)
    sparkle = tone(2400 + 800 * sparkle_t / sparkle_t[-1], [1.0]) * env(len(sparkle_t), 0.05, 0.8) * 0.25
    return norm(np.concatenate([first, gap, second, sparkle]), -10)


save("nap_yawn", yawn())
save("nap_snore", snore())
save("nap_wake", wake())
print("done")
