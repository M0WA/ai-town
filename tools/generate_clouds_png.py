#!/usr/bin/env python3
"""
generate_clouds_png.py
======================
Produces assets/textures/sky/clouds.png — a seamlessly tileable 1024x1024 RGBA
cloud texture for AI Town Phase 10b.

RGB channels: grey-white luminance (cloud body colour).
Alpha channel: cloud density mask (0 = fully transparent sky gap, 255 = dense cloud).

Algorithm
---------
Seamless tileability is achieved by constructing the noise field on a 2-D torus:
every sample is drawn from cos/sin functions whose period is exactly the image
width/height, so the left edge wraps perfectly to the right edge and the top to
the bottom.

Four octaves of "tileable Perlin-style" noise are summed with decreasing amplitude
(1/2, 1/4, 1/8, 1/16) to build an fBm field.  A non-linear power curve followed
by a contrast remap converts the raw noise into a cloud density mask, and the same
value is used (with a brightness boost) for the RGB luminance so the clouds look
like soft white/grey puffs against a transparent sky.

No external libraries are required beyond numpy and Pillow (both standard in the
AI Town dev environment).
"""

import math
import os
import struct
import time

import numpy as np
from PIL import Image

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
SIZE = 1024
OUTPUT_PATH = os.path.join(
    os.path.dirname(__file__), "..", "assets", "textures", "sky", "clouds.png"
)
SEED = 42

rng = np.random.default_rng(SEED)


# ---------------------------------------------------------------------------
# Tileable noise helpers
# ---------------------------------------------------------------------------
def tileable_noise_octave(size: int, frequency: int, phase_x: float, phase_y: float) -> np.ndarray:
    """
    Return a (size x size) float32 array in [-1, 1] representing one octave of
    tileable gradient noise at the given frequency.

    Tileability trick: project each pixel onto a 2-D torus via
        u = cos(2*pi*x / size),  v = sin(2*pi*x / size)
        s = cos(2*pi*y / size),  t = sin(2*pi*y / size)
    and evaluate a 4-D dot-product noise that is periodic by construction.
    """
    xs = np.linspace(0.0, 2.0 * math.pi * frequency, size, endpoint=False, dtype=np.float32)
    ys = np.linspace(0.0, 2.0 * math.pi * frequency, size, endpoint=False, dtype=np.float32)

    # Four torus dimensions, each periodic over the image period
    U = np.cos(xs + phase_x)  # (size,)
    V = np.sin(xs + phase_x)
    S = np.cos(ys + phase_y)  # (size,)
    T = np.sin(ys + phase_y)

    # Build 2-D arrays via outer products
    cos_x = U[np.newaxis, :]   # (1, size)
    sin_x = V[np.newaxis, :]
    cos_y = S[:, np.newaxis]   # (size, 1)
    sin_y = T[:, np.newaxis]

    # Combine — sum of products keeps the result in [-2, 2]; normalise to [-1, 1]
    field = (cos_x * cos_y + sin_x * sin_y + cos_x * sin_y + sin_x * cos_y) * 0.5
    return field.astype(np.float32)


def build_cloud_fbm(size: int) -> np.ndarray:
    """
    Fractional Brownian Motion: four octaves, each at a different frequency and
    random phase.  Returns a float32 array in roughly [-1, 1].
    """
    octaves = [
        (1, 1.0),
        (2, 0.5),
        (4, 0.25),
        (8, 0.125),
    ]
    total_weight = sum(w for _, w in octaves)

    field = np.zeros((size, size), dtype=np.float32)
    for freq, weight in octaves:
        phase_x = rng.uniform(0.0, 2.0 * math.pi)
        phase_y = rng.uniform(0.0, 2.0 * math.pi)
        field += weight * tileable_noise_octave(size, freq, phase_x, phase_y)

    field /= total_weight  # normalise to [-1, 1]
    return field


# ---------------------------------------------------------------------------
# Main generation
# ---------------------------------------------------------------------------
def generate(output_path: str) -> None:
    print(f"Generating {SIZE}x{SIZE} RGBA cloud texture…")

    fbm = build_cloud_fbm(SIZE)

    # Remap [-1, 1] → [0, 1]
    density = (fbm + 1.0) * 0.5

    # Non-linear contrast curve: sharpen the transition between sky and cloud.
    # power > 1 darkens mid-tones (makes sky gaps darker / more transparent)
    # then we shift and clamp to push mid-grey toward transparent.
    power = 2.2
    density = np.power(density, power)

    # Bias: shift so that roughly 55% of the sky is transparent (alpha < 10)
    # and the remaining 45% forms cloud bodies of varying density.
    density = density - 0.30
    density = np.clip(density, 0.0, 1.0)

    # Normalise so the brightest cloud is actually white
    max_val = density.max()
    if max_val > 1e-6:
        density /= max_val

    # Alpha channel: full density range [0, 255]
    alpha = (density * 255.0).astype(np.uint8)

    # RGB luminance: cloud white/grey.  Dense cloud (alpha=255) → near white (240).
    # Thin wisps (alpha≈64) → light grey.  Linear ramp keeps it soft.
    lum = np.clip(density * 240.0 + 40.0 * density, 0.0, 255.0).astype(np.uint8)

    # Assemble RGBA image
    rgba = np.stack([lum, lum, lum, alpha], axis=-1)  # (H, W, 4)
    img = Image.fromarray(rgba, mode="RGBA")

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    img.save(output_path, format="PNG")
    print(f"Saved: {os.path.abspath(output_path)}")
    print(f"  size={img.size}, mode={img.mode}")

    # Quick self-check
    with Image.open(output_path) as verify:
        assert verify.size == (SIZE, SIZE), f"Wrong size: {verify.size}"
        assert verify.mode == "RGBA", f"Wrong mode: {verify.mode}"
    print("Self-check PASSED: 1024x1024 RGBA confirmed.")


if __name__ == "__main__":
    generate(OUTPUT_PATH)
