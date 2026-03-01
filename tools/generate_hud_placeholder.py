#!/usr/bin/env python3
"""Generate placeholder HUD sprite sheet for AI Town Phase 8.

Produces a 2048x2048 RGBA8 PNG at assets/textures/ui/hud_sprites_ui.png.
Each sprite region is a colored rectangle with a text label identifying
the sprite purpose. This is an authoring artifact -- the runtime DDS
is produced later by export_textures.py (Phase 9).

IMPORTANT: No ICC profile or sRGB chunk is embedded. The PNG is written
with raw RGBA8 pixel values and no color-space conversion.
"""

import os
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
ATLAS_SIZE = 2048
OUTPUT_DIR = Path(__file__).resolve().parent.parent / "assets" / "textures" / "ui"
OUTPUT_FILE = OUTPUT_DIR / "hud_sprites_ui.png"

# Colors (RGBA tuples) -- distinct per category for visual clarity
COLOR_TOOLBAR = (70, 130, 180, 255)       # steel blue
COLOR_UTILITY = (100, 149, 237, 255)      # cornflower blue
COLOR_BELL = (218, 165, 32, 255)          # goldenrod
COLOR_BADGE = (220, 20, 60, 255)          # crimson
COLOR_SERVICE = (46, 139, 87, 255)        # sea green
COLOR_CURSOR = (148, 103, 189, 255)       # muted purple
COLOR_ZONE_CB = (189, 183, 107, 255)      # dark khaki
COLOR_SVC_CB = (143, 188, 143, 255)       # dark sea green
COLOR_MISC = (210, 105, 30, 255)          # chocolate
COLOR_BORDER = (40, 40, 40, 255)          # dark grey border
COLOR_TEXT = (255, 255, 255, 255)         # white text
COLOR_TRANSPARENT = (0, 0, 0, 0)         # fully transparent


def get_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    """Return a font at the requested size, falling back to the default."""
    # Try common system paths for a monospace or sans-serif font.
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    ]
    for path in candidates:
        if os.path.isfile(path):
            return ImageFont.truetype(path, size)
    # Last resort: Pillow built-in bitmap font (fixed size).
    return ImageFont.load_default()


def draw_sprite(
    draw: ImageDraw.ImageDraw,
    x: int,
    y: int,
    w: int,
    h: int,
    color: tuple[int, int, int, int],
    label: str,
    font: ImageFont.FreeTypeFont | ImageFont.ImageFont,
) -> None:
    """Draw a single placeholder sprite rectangle with a label."""
    # 1-pixel border
    draw.rectangle([x, y, x + w - 1, y + h - 1], outline=COLOR_BORDER, width=1)
    # Fill interior (inset by 1)
    draw.rectangle([x + 1, y + 1, x + w - 2, y + h - 2], fill=color)
    # Label -- center it as well as possible
    bbox = draw.textbbox((0, 0), label, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    tx = x + max(1, (w - tw) // 2)
    ty = y + max(1, (h - th) // 2)
    draw.text((tx, ty), label, fill=COLOR_TEXT, font=font)


def draw_pattern_diagonal_stripes(
    draw: ImageDraw.ImageDraw,
    x: int,
    y: int,
    w: int,
    h: int,
    color: tuple[int, int, int, int],
    label: str,
    font: ImageFont.FreeTypeFont | ImageFont.ImageFont,
) -> None:
    """45-degree diagonal stripe pattern (Residential zone / fire service)."""
    draw.rectangle([x, y, x + w - 1, y + h - 1], fill=color)
    stripe_color = (255, 255, 255, 180)
    spacing = 8
    for offset in range(-w, w + h, spacing):
        draw.line(
            [(x + max(0, offset), y + max(0, -offset)),
             (x + min(w - 1, offset + h), y + min(h - 1, h - 1 - max(0, offset)))],
            fill=stripe_color,
            width=2,
        )
    draw.text((x + 2, y + 2), label, fill=COLOR_TEXT, font=font)


def draw_pattern_horizontal_lines(
    draw: ImageDraw.ImageDraw,
    x: int,
    y: int,
    w: int,
    h: int,
    color: tuple[int, int, int, int],
    label: str,
    font: ImageFont.FreeTypeFont | ImageFont.ImageFont,
) -> None:
    """Horizontal line pattern (Commercial zone / police service)."""
    draw.rectangle([x, y, x + w - 1, y + h - 1], fill=color)
    stripe_color = (255, 255, 255, 180)
    spacing = 8
    for row in range(y + 4, y + h, spacing):
        draw.line([(x, row), (x + w - 1, row)], fill=stripe_color, width=2)
    draw.text((x + 2, y + 2), label, fill=COLOR_TEXT, font=font)


def draw_pattern_crosshatch(
    draw: ImageDraw.ImageDraw,
    x: int,
    y: int,
    w: int,
    h: int,
    color: tuple[int, int, int, int],
    label: str,
    font: ImageFont.FreeTypeFont | ImageFont.ImageFont,
) -> None:
    """Cross-hatch pattern (Industrial zone / water service)."""
    draw.rectangle([x, y, x + w - 1, y + h - 1], fill=color)
    stripe_color = (255, 255, 255, 180)
    spacing = 8
    # Horizontal lines
    for row in range(y + 4, y + h, spacing):
        draw.line([(x, row), (x + w - 1, row)], fill=stripe_color, width=1)
    # Vertical lines
    for col in range(x + 4, x + w, spacing):
        draw.line([(col, y), (col, y + h - 1)], fill=stripe_color, width=1)
    draw.text((x + 2, y + 2), label, fill=COLOR_TEXT, font=font)


def draw_pattern_dotted(
    draw: ImageDraw.ImageDraw,
    x: int,
    y: int,
    w: int,
    h: int,
    color: tuple[int, int, int, int],
    label: str,
    font: ImageFont.FreeTypeFont | ImageFont.ImageFont,
) -> None:
    """Dotted pattern (power service)."""
    draw.rectangle([x, y, x + w - 1, y + h - 1], fill=color)
    dot_color = (255, 255, 255, 200)
    spacing = 8
    for row in range(y + 4, y + h, spacing):
        for col in range(x + 4, x + w, spacing):
            draw.ellipse([col - 1, row - 1, col + 1, row + 1], fill=dot_color)
    draw.text((x + 2, y + 2), label, fill=COLOR_TEXT, font=font)


def main() -> None:
    # Create the RGBA image -- fully transparent background.
    img = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), COLOR_TRANSPARENT)
    draw = ImageDraw.Draw(img)

    # Choose font sizes for different sprite scales.
    font_large = get_font(11)
    font_small = get_font(9)
    font_tiny = get_font(7)

    # -----------------------------------------------------------------------
    # Row 0 (y:0-63) -- Toolbar icons, 48x48 each
    # -----------------------------------------------------------------------
    toolbar_icons = ["Zone", "Road", "Utilities", "Demolish", "Query"]
    for i, name in enumerate(toolbar_icons):
        draw_sprite(draw, x=i * 56, y=0, w=48, h=48,
                    color=COLOR_TOOLBAR, label=name, font=font_large)

    # -----------------------------------------------------------------------
    # Row 1 (y:64-127) -- Undo icon (48x48), Bell icon (48x48),
    #                      notification badges (16x16 each)
    # -----------------------------------------------------------------------
    draw_sprite(draw, x=0, y=64, w=48, h=48,
                color=COLOR_TOOLBAR, label="Undo", font=font_large)
    draw_sprite(draw, x=56, y=64, w=48, h=48,
                color=COLOR_BELL, label="Bell", font=font_large)

    # Notification badges -- 4 small 16x16 slots for badge numerals / states
    badge_labels = ["N:1", "N:2", "N:3", "N:!"]
    for i, lbl in enumerate(badge_labels):
        draw_sprite(draw, x=112 + i * 20, y=64, w=16, h=16,
                    color=COLOR_BADGE, label=lbl, font=font_tiny)

    # -----------------------------------------------------------------------
    # Row 2 (y:128-191) -- Service overlay toggle icons, 32x32 each
    # -----------------------------------------------------------------------
    service_icons = ["Fire", "Police", "Power", "Water"]
    for i, name in enumerate(service_icons):
        draw_sprite(draw, x=i * 40, y=128, w=32, h=32,
                    color=COLOR_SERVICE, label=name, font=font_small)

    # -----------------------------------------------------------------------
    # Row 3 (y:192-255) -- Cursor variants, 32x32 each
    #   Zone crosshair, Road cursor, Utilities cursor,
    #   Demolish X, Query magnifier, Zoom, Pan, Rotate
    # -----------------------------------------------------------------------
    cursor_labels = [
        "Cur:Zone", "Cur:Road", "Cur:Util",
        "Cur:Demo", "Cur:Qry", "Cur:Zoom",
        "Cur:Pan", "Cur:Rot",
    ]
    for i, lbl in enumerate(cursor_labels):
        draw_sprite(draw, x=i * 40, y=192, w=32, h=32,
                    color=COLOR_CURSOR, label=lbl, font=font_tiny)

    # -----------------------------------------------------------------------
    # Row 4 (y:256-319) -- Zone colorblind patterns, 64x64 each
    #   Residential = 45-degree diagonal stripes
    #   Commercial  = horizontal lines
    #   Industrial  = cross-hatch
    # -----------------------------------------------------------------------
    draw_pattern_diagonal_stripes(
        draw, x=0, y=256, w=64, h=64,
        color=COLOR_ZONE_CB, label="R:Diag", font=font_small,
    )
    draw_pattern_horizontal_lines(
        draw, x=72, y=256, w=64, h=64,
        color=COLOR_ZONE_CB, label="C:Horiz", font=font_small,
    )
    draw_pattern_crosshatch(
        draw, x=144, y=256, w=64, h=64,
        color=COLOR_ZONE_CB, label="I:XHatch", font=font_small,
    )

    # -----------------------------------------------------------------------
    # Row 5 (y:320-383) -- Service colorblind patterns, 64x64 each
    #   Fire   = diagonal hatching
    #   Police = horizontal lines
    #   Power  = dotted
    #   Water  = cross-hatch
    # -----------------------------------------------------------------------
    draw_pattern_diagonal_stripes(
        draw, x=0, y=320, w=64, h=64,
        color=COLOR_SVC_CB, label="F:Diag", font=font_small,
    )
    draw_pattern_horizontal_lines(
        draw, x=72, y=320, w=64, h=64,
        color=COLOR_SVC_CB, label="P:Horiz", font=font_small,
    )
    draw_pattern_dotted(
        draw, x=144, y=320, w=64, h=64,
        color=COLOR_SVC_CB, label="Pw:Dot", font=font_small,
    )
    draw_pattern_crosshatch(
        draw, x=216, y=320, w=64, h=64,
        color=COLOR_SVC_CB, label="W:XHat", font=font_small,
    )

    # -----------------------------------------------------------------------
    # Row 6 (y:384-447) -- Misc icons
    #   Demolish confirmation badge (48x48)
    #   Unsaved changes dot (16x16)
    #   Clock icon for grace period (16x16)
    # -----------------------------------------------------------------------
    draw_sprite(draw, x=0, y=384, w=48, h=48,
                color=COLOR_MISC, label="DemoBdg", font=font_small)
    draw_sprite(draw, x=56, y=384, w=16, h=16,
                color=(218, 165, 32, 255), label=".", font=font_large)  # amber dot
    draw_sprite(draw, x=80, y=384, w=16, h=16,
                color=COLOR_MISC, label="Clk", font=font_tiny)

    # -----------------------------------------------------------------------
    # Save -- no ICC profile, no sRGB chunk, raw RGBA8
    # -----------------------------------------------------------------------
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Pillow's PNG writer by default does not embed an ICC profile.
    # Explicitly strip any info dict entries that could inject color metadata.
    img.info.pop("icc_profile", None)
    img.info.pop("srgb", None)
    img.info.pop("gamma", None)
    img.info.pop("chromaticity", None)

    # Save with pnginfo=None to suppress any ancillary chunks.
    img.save(str(OUTPUT_FILE), format="PNG", icc_profile=None, pnginfo=None)

    print(f"Wrote {OUTPUT_FILE}  ({ATLAS_SIZE}x{ATLAS_SIZE} RGBA8)")
    print(f"File size: {OUTPUT_FILE.stat().st_size:,} bytes")


if __name__ == "__main__":
    main()
