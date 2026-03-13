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
Seamlessly tileable Perlin-style noise is achieved by:

  1. Treating each pixel (ix, iy) as lying in a fractional grid of size
     [0, N) where N is an integer period (power of two).
  2. Taking all integer lattice indices modulo N before looking up the
     gradient / permutation table.  This guarantees that lattice cell
     (N-1, _) and lattice cell (0, _) share the same gradient, so the
     noise field repeats perfectly after N grid cells.
  3. For an image of size SIZE pixels and an octave at grid frequency F
     (i.e. F complete noise tiles across the image), the period is:
         N = SIZE / F
     We restrict F to integer powers of 2 so N is always a power of 2.
  4. FBM accumulates octaves at F = 1, 2, 4, 8, … grid frequencies up to
     F = SIZE/2.

Domain warping is applied in pixel space using warp FBMs evaluated at
F = 1 and F = 2.  Because the warp fields are themselves tileable (built
with the same period-wrapping noise), and the displacements are small
relative to the coarsest period, the seam error is bounded.  The
maximum pixel displacement is 32 px, which is less than the coarsest
cell size (SIZE / 1 = 1024 px), so the displaced sample still lives
inside the periodic cell and the modular lookup wraps correctly.

Cloud morphology is shaped by:
  - A multi-octave FBM with domain warping for swirling cumulus billows.
  - A low-frequency cluster envelope that creates large clear-sky regions.
  - A thin-wisp overlay (high-power falloff) for cirrus-like edge fringing.
  - Smoothstep contrast + power curve to separate sky gaps from cloud cores.
  - Sqrt luminance lift so dense cloud tops are bright white and thin wisps
    are light grey rather than mid-grey.

No external libraries required beyond numpy and Pillow.
"""

import math
import os

import numpy as np
from PIL import Image

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
SIZE   = 1024       # output image size in pixels (must be power of two)
SEED   = 137
OUTPUT_PATH = os.path.join(
    os.path.dirname(__file__), "..", "assets", "textures", "sky", "clouds.png"
)

rng = np.random.default_rng(SEED)


# ---------------------------------------------------------------------------
# Tileable 2-D Perlin-style gradient noise
# ---------------------------------------------------------------------------
# We use a classical Perlin permutation-table approach but wrap the integer
# cell indices modulo the period, giving exact seamless tiling.

PERM_SIZE = 512   # power of two; larger → longer before self-aliasing
_PERM:    np.ndarray | None = None   # (PERM_SIZE * 2,) int32
_GRAD2:   np.ndarray | None = None   # (PERM_SIZE, 2) float32


def _init_perm() -> None:
    global _PERM, _GRAD2
    if _PERM is not None:
        return
    perm = np.arange(PERM_SIZE, dtype=np.int32)
    rng.shuffle(perm)
    _PERM  = np.concatenate([perm, perm]).astype(np.int32)
    # 2-D unit gradient vectors evenly distributed on the unit circle
    angles = np.linspace(0, 2 * math.pi, PERM_SIZE, endpoint=False,
                         dtype=np.float32)
    # Rotate by a random phase so we don't always start at (1,0)
    angles += rng.uniform(0, 2 * math.pi)
    _GRAD2 = np.stack([np.cos(angles), np.sin(angles)], axis=-1)


def _tileable_perlin_2d(
    px: np.ndarray,   # (H, W) float32, pixel-space x in [0, SIZE)
    py: np.ndarray,   # (H, W) float32, pixel-space y in [0, SIZE)
    period: int,      # grid cells before tiling; must divide SIZE evenly
) -> np.ndarray:
    """
    Evaluate one octave of tileable 2-D gradient noise.

    px, py are coordinates in [0, SIZE).  The noise field repeats with a
    grid period of `period` cells.  Cell size = SIZE / period pixels.

    Returns (H, W) float32 in approximately [-1, 1].
    """
    _init_perm()
    P = _PERM
    G = _GRAD2
    PMASK = PERM_SIZE - 1

    cell_size = SIZE / period   # pixels per grid cell

    # Fractional grid coordinates
    gx = px / cell_size   # (H, W)
    gy = py / cell_size

    # Integer cell corners
    ix0 = np.floor(gx).astype(np.int32)
    iy0 = np.floor(gy).astype(np.int32)
    ix1 = ix0 + 1
    iy1 = iy0 + 1

    # Wrap modulo period — this is what makes tiling exact
    ix0m = ix0 % period
    ix1m = ix1 % period
    iy0m = iy0 % period
    iy1m = iy1 % period

    # Fractional offsets inside the cell
    fx = gx - ix0.astype(np.float32)
    fy = gy - iy0.astype(np.float32)

    # Quintic smoothstep (C2 continuity)
    ux = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0)
    uy = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0)

    # Gradient lookup via permutation table
    def grad_dot(ixm, iym, dfx, dfy):
        idx = P[(P[ixm & PMASK] + iym) & PMASK]
        g   = G[idx]                     # (H, W, 2)
        return g[..., 0] * dfx + g[..., 1] * dfy

    n00 = grad_dot(ix0m, iy0m, fx,     fy    )
    n10 = grad_dot(ix1m, iy0m, fx-1.0, fy    )
    n01 = grad_dot(ix0m, iy1m, fx,     fy-1.0)
    n11 = grad_dot(ix1m, iy1m, fx-1.0, fy-1.0)

    # Bilinear interpolation with smoothstepped weights
    nx0 = n00 + ux * (n10 - n00)
    nx1 = n01 + ux * (n11 - n01)
    return (nx0 + uy * (nx1 - nx0)).astype(np.float32)


def tileable_fbm(
    px: np.ndarray,       # (H, W) float32 pixel-x in [0, SIZE)
    py: np.ndarray,       # (H, W) float32 pixel-y in [0, SIZE)
    base_period: int = 2, # coarsest grid period (number of full cycles)
    octaves:     int = 8,
    persistence: float = 0.52,
    lacunarity:  float = 2.0,   # must be integer or period_i*2 to stay power-of-2
) -> np.ndarray:
    """
    Multi-octave tileable FBM.  Each octave doubles the frequency (halves
    the cell size) so every octave period remains an integer that divides
    SIZE.  Returns (H, W) float32 in approximately [-1, 1].
    """
    field = np.zeros_like(px)
    amp   = 1.0
    total = 0.0
    period = base_period

    for _ in range(octaves):
        # Guard: period must be at least 1 and SIZE must be divisible
        if period > SIZE:
            break
        field += amp * _tileable_perlin_2d(px, py, period)
        total += amp
        amp    *= persistence
        period  = int(period * lacunarity)
        if period > SIZE:
            break

    return (field / total).astype(np.float32)


# ---------------------------------------------------------------------------
# Pixel coordinate grids
# ---------------------------------------------------------------------------

def _pixel_grids(size: int) -> tuple[np.ndarray, np.ndarray]:
    """Return (px, py) each (size, size) float32 in [0, size)."""
    idx = np.arange(size, dtype=np.float32)
    px  = np.broadcast_to(idx[np.newaxis, :], (size, size)).copy()
    py  = np.broadcast_to(idx[:, np.newaxis], (size, size)).copy()
    return px, py


# ---------------------------------------------------------------------------
# Cloud field construction
# ---------------------------------------------------------------------------

def build_cloud_field(size: int) -> tuple[np.ndarray, np.ndarray]:
    """
    Returns (main_field, cluster_mask):
        main_field   — (H, W) float32 in [-1, 1]
        cluster_mask — (H, W) float32 in [ 0, 1]
    """
    px, py = _pixel_grids(size)

    # ------------------------------------------------------------------
    # 1. Warp fields — low-frequency tileable FBMs produce pixel offsets
    #    Maximum displacement: warp_amp pixels.  Keeping warp_amp well
    #    below (size / base_period) ensures the displaced sample stays
    #    within the nearest periodic repeat.
    # ------------------------------------------------------------------
    print("  Computing warp fields…")
    warp_amp = 32.0   # pixels; coarsest cell = 512 px at period=2
    wx = tileable_fbm(px, py, base_period=2, octaves=4,
                      persistence=0.55) * warp_amp
    wy = tileable_fbm(px, py, base_period=2, octaves=4,
                      persistence=0.55) * warp_amp

    # Displace sampling coordinates, wrapping modulo size for perfect tiling
    px_w = (px + wx) % size
    py_w = (py + wy) % size

    # ------------------------------------------------------------------
    # 2. Main FBM on warped coordinates
    # ------------------------------------------------------------------
    print("  Computing main cloud FBM (8 octaves, warped)…")
    field = tileable_fbm(px_w, py_w, base_period=2, octaves=8,
                         persistence=0.52)

    # ------------------------------------------------------------------
    # 3. Cluster mask — very coarse, no warp
    # ------------------------------------------------------------------
    print("  Computing cluster mask…")
    cl_raw  = tileable_fbm(px, py, base_period=1, octaves=3,
                           persistence=0.65)
    lo, hi  = float(cl_raw.min()), float(cl_raw.max())
    cluster = (cl_raw - lo) / (hi - lo + 1e-9)
    # Smoothstep → soft patch boundaries
    cluster = cluster * cluster * (3.0 - 2.0 * cluster)
    # Power to tune sky coverage (~55% clear sky / 45% cloud)
    cluster = np.power(cluster, 2.0).astype(np.float32)

    return field, cluster


# ---------------------------------------------------------------------------
# Density shaping + luminance
# ---------------------------------------------------------------------------

def shape_density(
    field:   np.ndarray,
    cluster: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    """Convert (field, cluster) → (alpha, lum) uint8."""
    # Remap [-1,1] → [0,1]
    density = (field + 1.0) * 0.5

    # Smoothstep — soft mid-tone contrast
    density = density * density * (3.0 - 2.0 * density)

    # Power curve — compress sky gaps, preserve cloud peaks
    density = np.power(density, 1.4)

    # Bias threshold — controls sky / cloud split point
    density = np.clip(density - 0.30, 0.0, 1.0)

    # Cluster mask — large clear-sky regions
    density = density * cluster

    # Re-normalise so peak cloud = 1.0
    peak = float(density.max())
    if peak > 1e-6:
        density /= peak

    # Thin wisp layer — faint fringe at cloud edges (cirrus-like)
    wisp = (field + 1.0) * 0.5
    wisp = np.power(np.clip(wisp - 0.56, 0.0, 1.0), 3.0) * 0.22
    density = np.clip(density + wisp, 0.0, 1.0).astype(np.float32)

    # Alpha: full 0–255
    alpha = (density * 255.0 + 0.5).astype(np.uint8)

    # Luminance: sqrt lift → bright cloud tops, natural grey wisps
    lum_f = np.sqrt(density) * 230.0 + density * 25.0
    lum   = np.clip(lum_f, 0.0, 255.0).astype(np.uint8)

    return alpha, lum


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def generate(output_path: str) -> None:
    print(f"Generating {SIZE}x{SIZE} RGBA cloud texture "
          f"(tileable Perlin FBM + domain warp)…")

    field, cluster = build_cloud_field(SIZE)
    alpha, lum     = shape_density(field, cluster)

    rgba = np.stack([lum, lum, lum, alpha], axis=-1)
    img  = Image.fromarray(rgba, mode="RGBA")

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    img.save(output_path, format="PNG")
    print(f"Saved: {os.path.abspath(output_path)}")
    print(f"  size={img.size}, mode={img.mode}")

    # Seam check
    arr = np.array(img).astype(np.int32)
    lr  = float(np.abs(arr[:,  0, :] - arr[:, -1, :]).mean())
    tb  = float(np.abs(arr[0,  :, :] - arr[-1, :, :]).mean())
    print(f"  Seam check — left/right: {lr:.2f}  top/bottom: {tb:.2f}  "
          f"(0 = perfect seamless; < 8 is excellent)")

    # Coverage stats
    a         = alpha.astype(np.float32)
    cov_any   = float((a >  10).sum()) / a.size * 100.0
    cov_dense = float((a > 180).sum()) / a.size * 100.0
    print(f"  Coverage: any cloud (alpha>10): {cov_any:.1f}%  "
          f"dense cores (alpha>180): {cov_dense:.1f}%")

    with Image.open(output_path) as v:
        assert v.size == (SIZE, SIZE), f"Wrong size: {v.size}"
        assert v.mode == "RGBA",       f"Wrong mode: {v.mode}"
    print("Self-check PASSED: 1024x1024 RGBA confirmed.")


if __name__ == "__main__":
    generate(OUTPUT_PATH)
