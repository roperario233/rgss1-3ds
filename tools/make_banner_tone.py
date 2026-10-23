#!/usr/bin/env python3
"""
make_banner_tone.py -- turn the title theme into the banner's CWAV source.

The HOME menu plays a banner's tune on a loop while the tile is selected, so a
plain excerpt would click every time it wrapped. This cuts a phrase out of the
game's own title theme, crossfades its tail back over its head, and writes a
WAV that bannertool encodes as PCM.

    the game's Audio/BGM/To the Moon (Title Theme).ogg   (from your own copy)
        -> work/banner/title.ogg     the extracted original
        -> work/banner/tone.wav      2.8 s, 32768 Hz, stereo, loop-safe

2.8 s and two channels are not taste: 3dbrew's CBMD page says a CXI banner's
BCWAV must have two channels and be 3 seconds or less, or the menu beeps, clicks
or fails to load the banner at all.

The loop trick
--------------
Take L + X seconds of the track, keep the first L as the loop, and mix the
extra X seconds into the head with an equal-power fade:

    out[0..X] = head * fade_in + tail * fade_out

so the last sample of the loop is followed by the sample that came after it in
the original, and the wrap is inaudible. Both gains are square-root shaped,
which keeps the perceived loudness flat across the join (a linear fade dips).

The source is game audio: this WAV, the CWAV and the CIA that carries it are for
this console only, exactly as the engine's audio pack already is.

Usage:  python tools/make_banner_tone.py [--seconds 2.8] [--seam 0.5]
"""

import argparse
import array
import math
import os
import subprocess
import sys
import wave
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "work", "banner")

ZIP = os.path.join(ROOT, "To.the.Moon.v2023.07.03.Incl.ALL.DLC.zip")
TRACK = "Audio/BGM/To the Moon (Title Theme) - Kan R. Gao.ogg"
OGG = os.path.join(OUT, "title.ogg")
WAV = os.path.join(OUT, "tone.wav")

RATE = 32768          # the 3DS DSP's own rate; bannertool encodes from it
CHANNELS = 2
PEAK = 0.84           # -1.5 dBFS, so nothing clips after ADPCM


def ffmpeg():
    try:
        subprocess.run(["ffmpeg", "-version"], check=True, capture_output=True)
    except (OSError, subprocess.CalledProcessError):
        raise SystemExit("ffmpeg not found on PATH (winget install Gyan.FFmpeg)")
    return "ffmpeg"


def extract_track():
    """Pull the title theme out of the game's zip, without unpacking the rest."""
    if os.path.exists(OGG):
        print("[i] %s already extracted" % os.path.basename(OGG))
        return
    if not os.path.exists(ZIP):
        raise SystemExit("missing %s -- the game package, needed once" % ZIP)
    with zipfile.ZipFile(ZIP) as z:
        hits = [n for n in z.namelist() if TRACK in n]
        if not hits:
            raise SystemExit("the archive has no %s" % TRACK)
        data = z.read(hits[0])
    with open(OGG, "wb") as fh:
        fh.write(data)
    print("[+] %-16s %d bytes (from %s)" % (os.path.basename(OGG), len(data),
                                           os.path.basename(ZIP)))


def decode(seconds, seam):
    """Decode the first seconds+seam of the track to raw PCM at the DSP's rate."""
    raw = os.path.join(OUT, "tone_raw.wav")
    subprocess.run(
        [ffmpeg(), "-y", "-hide_banner", "-loglevel", "error", "-i", OGG,
         "-t", "%.3f" % (seconds + seam), "-ar", str(RATE), "-ac", str(CHANNELS),
         "-c:a", "pcm_s16le", raw],
        check=True)
    with wave.open(raw, "rb") as w:
        if (w.getnchannels() != CHANNELS or w.getsampwidth() != 2
                or w.getframerate() != RATE):
            raise SystemExit("ffmpeg produced an unexpected WAV format")
        frames = w.readframes(w.getnframes())
    os.remove(raw)
    return array.array("h", frames)


def loop_it(samples, seconds, seam):
    """Crossfade the tail back over the head, giving a seamless loop."""
    total = int(round(seconds * RATE))
    fade = int(round(seam * RATE))
    if len(samples) < (total + fade) * CHANNELS:
        raise SystemExit("the track is shorter than %.1f s" % (seconds + seam))

    out = array.array("h", samples[:total * CHANNELS])
    for i in range(fade):
        t = i / float(fade - 1)
        head_gain = math.sin(t * math.pi / 2)      # equal power, rising
        tail_gain = math.cos(t * math.pi / 2)      # equal power, falling
        for ch in range(CHANNELS):
            a = samples[i * CHANNELS + ch]
            b = samples[(total + i) * CHANNELS + ch]
            out[i * CHANNELS + ch] = int(round(a * head_gain + b * tail_gain))
    return out


def normalise(samples):
    """Scale to PEAK full scale; returns the samples and the peak found."""
    peak = max(abs(min(samples)), abs(max(samples))) or 1
    gain = (PEAK * 32767.0) / peak
    return array.array("h", [int(round(s * gain)) for s in samples]), peak


def seam_step(samples):
    """The jump from the last sample of the loop back to the first, per channel.

    Compared with the largest ordinary step in the first tenth of a second: if
    the wrap is in the same range, there is nothing to hear.
    """
    last = len(samples) - CHANNELS
    wrap = max(abs(samples[ch] - samples[last + ch]) for ch in range(CHANNELS))
    nearby = max(abs(samples[i] - samples[i - CHANNELS])
                 for i in range(CHANNELS, min(len(samples), 4000)))
    return wrap, nearby


def main():
    ap = argparse.ArgumentParser(description="Build the banner's tone WAV.")
    ap.add_argument("--seconds", type=float, default=2.8,
                    help="loop length in seconds (default 2.8; the menu allows 3)")
    ap.add_argument("--seam", type=float, default=0.5,
                    help="crossfade length in seconds (default 0.5)")
    args = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    extract_track()
    samples = decode(args.seconds, args.seam)
    loop, peak = normalise(loop_it(samples, args.seconds, args.seam))

    with wave.open(WAV, "wb") as w:
        w.setnchannels(CHANNELS)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(loop.tobytes())

    wrap, nearby = seam_step(loop)
    print("[+] %-16s %.1f s, %d Hz, %d ch, %d bytes"
          % (os.path.basename(WAV), args.seconds, RATE, CHANNELS,
             os.path.getsize(WAV)))
    print("[i] peak before normalising: %d of 32767; loop seam step %d "
          "(ordinary steps nearby reach %d)" % (peak, wrap, nearby))
    print("[i] source: %s (game audio: this console only)" % TRACK)
    return 0


if __name__ == "__main__":
    sys.exit(main())

