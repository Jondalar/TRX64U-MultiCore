#!/usr/bin/env python3
"""Generate and validate the TRX64 MultiCore logo family (T114)."""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def logo_dir(core_id: str) -> Path:
    return ROOT / "cores" / core_id / "logo"

BG = "#070A12"
INK = "#F4F2FB"
MUTED = "#9DA6BD"
CBM_MARK_BLUE = "#4E6B88"
CBM_MARK_RED = "#EF4B45"


# A build_only core whose .cor still goes to the board in the Gitea package needs an icon for the packer: list it here. c64scpu was one (T182 step 8,
# 2026-11-05) until it left build_only on 2026-11-18; none is now.
LOGO_WHILE_BUILD_ONLY: set[str] = set()


def definitions() -> dict[str, str]:
    """Core id -> label of the contact sheet."""
    return {
        "amiga": "Amiga", "amiga1200": "Amiga 1200", "apple2": "Apple IIe", "atari800": "Atari 800 XL",
        "c128": "Commodore 128", "c64scpu": "C64 SuperCPU", "c264": "Commodore 264", "c64u": "C64U", "cpc": "Amstrad CPC",
        "ddt_mandel": "DDT Mandelbrot", "ddt_novic": "DDT No VIC",
        "neogeo": "Neo Geo", "nes": "NES", "pet": "Commodore PET", "snes": "SNES", "vc20": "VIC-20", "zx": "ZX Spectrum", "zxnext": "ZX Spectrum Next",
    }


# T114 D5 / T115 D7: standalone identities that have no shipped core descriptor.
def extra_dir(identity: str) -> Path:
    return HERE / identity


def extra_definitions() -> dict[str, str]:
    return {"u64": "U64", "firmware": "Firmware", "trx64": "TRX64"}


def core_ids() -> set[str]:
    result = set()
    paths = list((ROOT / "cores").glob("*/core.toml")) + list((ROOT / "cores").glob("*/mono/core.toml"))
    for path in paths:
        source = path.read_text(encoding="utf-8")
        match = re.search(r'^id\s*=\s*"([^"]+)"\s*$', source, re.MULTILINE)
        if not match:
            raise RuntimeError(f"{path.relative_to(ROOT)} has no quoted id")
        core_id = match.group(1)
        if re.search(r'^build_only\s*=\s*true\b', source, re.MULTILINE) and core_id not in LOGO_WHILE_BUILD_ONLY:
            continue    # T115: a build-only core (c64ref) has no menu entry, so no logo
        if core_id == "null":
            continue    # flash filler, never a selectable core
        result.add(core_id)
    # T115: cores/c64u has no descriptor any more (source tree, never built); C64U remains a distinct platform identity instead of
    # being reused for Gideon's standalone U64 mark. TRX64 is a .cfw identity without a core directory: an extra (tools/core_logos/trx64).
    result |= {"c64u"}
    return result


def rgb565(image: Image.Image) -> bytes:
    out = bytearray()
    for r, g, b in image.convert("RGB").getdata():
        out += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return bytes(out)


def _rgb(hexcol: str) -> tuple[int, int, int]:
    return int(hexcol[1:3], 16), int(hexcol[3:5], 16), int(hexcol[5:7], 16)


ICON_MASTER = 256
ICON_COLOURS = {_rgb(BG), _rgb(INK), _rgb(CBM_MARK_BLUE), _rgb(CBM_MARK_RED)}


def centred_text(draw: ImageDraw.ImageDraw, value: str, size: int, *, y_adjust: int = 0) -> None:
    face = font(size, True)
    box = draw.textbbox((0, 0), value, font=face)
    width, height = box[2] - box[0], box[3] - box[1]
    draw.text(((ICON_MASTER - width) // 2 - box[0], (ICON_MASTER - height) // 2 - box[1] + y_adjust),
              value, fill=INK, font=face)


def commodore_symbol(draw: ImageDraw.ImageDraw) -> None:
    """Classic two-colour C= mark, using a real C glyph rather than a clipped circle."""
    face = font(188, True)
    box = draw.textbbox((0, 0), "C", font=face)
    height = box[3] - box[1]
    draw.text((20 - box[0], (ICON_MASTER - height) // 2 - box[1]), "C", fill=CBM_MARK_BLUE, font=face)
    draw.polygon(((156, 102), (218, 102), (202, 121), (156, 121)), fill=CBM_MARK_BLUE)
    draw.polygon(((156, 130), (202, 130), (218, 149), (156, 149)), fill=CBM_MARK_RED)


def cubic_points(p0: tuple[int, int], p1: tuple[int, int], p2: tuple[int, int],
                 p3: tuple[int, int], steps: int = 18) -> list[tuple[float, float]]:
    result = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1.0 - t
        result.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                       u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
    return result


def apple_symbol(draw: ImageDraw.ImageDraw) -> None:
    # A smooth, reduced Apple silhouette: shoulder/notch, two lower lobes and the bite are visible at 32 px.
    start = (128, 70)
    segments = [
        ((105, 51), (75, 55), (57, 82)),
        ((34, 116), (40, 166), (61, 198)),
        ((76, 221), (92, 231), (112, 218)),
        ((123, 210), (133, 210), (145, 219)),
        ((163, 230), (178, 219), (193, 197)),
        ((214, 166), (219, 124), (204, 92)),
        ((190, 64), (159, 51), (140, 66)),
        ((135, 70), (131, 72), start),
    ]
    outline: list[tuple[float, float]] = [start]
    current = start
    for c1, c2, end in segments:
        outline.extend(cubic_points(current, c1, c2, end))
        current = end
    draw.polygon(outline, fill=INK)
    draw.ellipse((183, 83, 231, 131), fill=BG)

    leaf_start = (124, 58)
    leaf = [leaf_start]
    leaf.extend(cubic_points(leaf_start, (124, 34), (143, 13), (171, 9), 14))
    leaf.extend(cubic_points((171, 9), (171, 34), (153, 55), leaf_start, 14))
    draw.polygon(leaf, fill=INK)


def atari_symbol(draw: ImageDraw.ImageDraw) -> None:
    draw.line((128, 35, 128, 211), fill=INK, width=22)
    draw.polygon(((92, 38), (115, 38), (92, 143), (38, 222), (13, 222), (66, 139)), fill=INK)
    draw.polygon(((141, 38), (164, 38), (190, 139), (243, 222), (218, 222), (164, 143)), fill=INK)


def mandelbrot_symbol(draw: ImageDraw.ImageDraw) -> None:
    """The set itself (T168), 64 iterations, drawn at the master size over x -1.95 .. 0.5, y -1.225 .. 1.225: what the core renders."""
    for py in range(ICON_MASTER):
        for px in range(ICON_MASTER):
            c = complex(-1.95 + 2.45 * px / ICON_MASTER, -1.225 + 2.45 * py / ICON_MASTER)
            z = 0j
            for _ in range(64):
                z = z * z + c
                if abs(z) > 2.0:
                    break
            else:
                draw.point((px, py), fill=INK)


def xor_symbol(draw: ImageDraw.ImageDraw) -> None:
    """The AND/XOR texture the demo's background is made of (T168): 8 x 8 cells of 28 px with a margin, a cell lit when its column and row share no bit."""
    cell, margin = 28, (ICON_MASTER - 8 * 28) // 2
    for j in range(8):
        for i in range(8):
            if i & j == 0:
                draw.rectangle((margin + i * cell, margin + j * cell, margin + (i + 1) * cell - 1, margin + (j + 1) * cell - 1), fill=INK)


def firmware_symbol(draw: ImageDraw.ImageDraw) -> None:
    draw.rounded_rectangle((52, 52, 204, 204), radius=16, outline=INK, width=18)
    draw.rounded_rectangle((91, 91, 165, 165), radius=8, outline=INK, width=14)
    for p in (78, 119, 160):
        draw.rectangle((p, 24, p + 15, 52), fill=INK)
        draw.rectangle((p, 204, p + 15, 232), fill=INK)
        draw.rectangle((24, p, 52, p + 15), fill=INK)
        draw.rectangle((204, p, 232, p + 15), fill=INK)


def flat_icon_master(image: Image.Image) -> Image.Image:
    """Snap master rasterisation to its flat design colours; the final resize owns all antialiasing."""
    colours = tuple(ICON_COLOURS)
    result = image.convert("RGB")
    pixels = result.load()
    for y in range(result.height):
        for x in range(result.width):
            colour = pixels[x, y]
            if colour not in ICON_COLOURS:
                pixels[x, y] = min(colours, key=lambda target: sum((colour[i] - target[i]) ** 2 for i in range(3)))
    return result


def icon_master(identity: str) -> Image.Image:
    """One large monochrome platform cue, intentionally free of model numbers and decoration."""
    image = Image.new("RGB", (ICON_MASTER, ICON_MASTER), BG)
    draw = ImageDraw.Draw(image)
    if identity in {"c64u", "c264", "c128", "vc20", "pet", "c64scpu"}:
        commodore_symbol(draw)
    elif identity in {"amiga", "amiga1200"}:
        centred_text(draw, "A", 206, y_adjust=-4)
    elif identity == "apple2":
        apple_symbol(draw)
    elif identity == "atari800":
        atari_symbol(draw)
    elif identity == "nes":
        centred_text(draw, "NES", 93, y_adjust=-2)
    elif identity == "neogeo":
        centred_text(draw, "NEO", 93, y_adjust=-2)
    elif identity == "cpc":
        centred_text(draw, "CPC", 83, y_adjust=-2)
    elif identity == "snes":
        centred_text(draw, "SNES", 70, y_adjust=-2)
    elif identity == "zx":
        centred_text(draw, "ZX", 123, y_adjust=-3)
    elif identity == "zxnext":
        centred_text(draw, "N", 198, y_adjust=-4)
    elif identity == "trx64":
        centred_text(draw, "TRX", 91, y_adjust=-2)
    elif identity == "u64":
        centred_text(draw, "U", 190, y_adjust=-4)
    elif identity == "firmware":
        firmware_symbol(draw)
    elif identity == "ddt_mandel":
        mandelbrot_symbol(draw)
    elif identity == "ddt_novic":
        xor_symbol(draw)
    else:
        raise KeyError(f"no 32 px platform symbol for {identity}")
    return flat_icon_master(image)


def rgb565_preview(image: Image.Image) -> Image.Image:
    """Quantise to RGB565 and expand the stored bits as the display does, so the PNG is an honest preview."""
    result = Image.new("RGB", image.size)
    src, dst = image.convert("RGB").load(), result.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b = src[x, y]
            r5, g6, b5 = r >> 3, g >> 2, b >> 3
            dst[x, y] = ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))
    return result


def pixel_icon(identity: str) -> Image.Image:
    """Return the final 32 x 32 RGB565-preview icon from its simple high-resolution symbol."""
    return rgb565_preview(icon_master(identity).resize((32, 32), Image.Resampling.LANCZOS))


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = [
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold else "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ]
    for candidate in candidates:
        if Path(candidate).exists():
            return ImageFont.truetype(candidate, size)
    return ImageFont.load_default()


def asset_dir(identity: str) -> Path:
    return extra_dir(identity) if identity in extra_definitions() else logo_dir(identity)


PIXEL_COLS = 4
PIXEL_CARD_W = 238
PIXEL_CARD_H = 205
PIXEL_SCALE = 5


def pixel_contact_sheet_size(n: int) -> tuple[int, int]:
    rows = (n + PIXEL_COLS - 1) // PIXEL_COLS
    return (1000, 118 + rows * PIXEL_CARD_H + 22)


def make_pixel_contact_sheet(items: dict[str, str]) -> None:
    sheet = Image.new("RGB", pixel_contact_sheet_size(len(items)), BG)
    draw = ImageDraw.Draw(sheet)
    draw.text((48, 27), "NATIVE 32 PX CORE ICONS", fill=INK, font=font(34, True))
    draw.text((50, 74), "5× nearest-neighbour preview  ·  one reduced symbol per platform", fill=MUTED, font=font(17))
    for index, (identity, label) in enumerate(items.items()):
        col, row = index % PIXEL_COLS, index // PIXEL_COLS
        x, y = 35 + col * PIXEL_CARD_W, 118 + row * PIXEL_CARD_H
        icon = Image.open(asset_dir(identity) / "logo-32.png").convert("RGB")
        icon = icon.resize((32 * PIXEL_SCALE, 32 * PIXEL_SCALE), Image.Resampling.NEAREST)
        sheet.paste(icon, (x + 31, y))
        draw.text((x + 111, y + 169), label.upper(), fill=INK, font=font(17, True), anchor="ma")
        draw.text((x + 111, y + 190), identity, fill=MUTED, font=font(13), anchor="ma")
    sheet.save(HERE / "contact-sheet-32.png", optimize=True)


def display_items(items: dict[str, str], extras: dict[str, str]) -> dict[str, str]:
    order = ["trx64", "u64", "c64u", "c128", "c64scpu", "c264", "vc20", "pet", "amiga", "amiga1200",
             "apple2", "atari800", "cpc", "neogeo", "ddt_mandel", "ddt_novic", "nes", "snes", "zx", "zxnext"]
    all_items = {**items, "u64": extras["u64"], "trx64": extras["trx64"]}
    if set(order) != set(all_items):
        raise RuntimeError(f"contact-sheet identities differ: {sorted(set(order) ^ set(all_items))}")
    return {identity: all_items[identity] for identity in order}


def build() -> None:
    items = definitions()
    extras = extra_definitions()
    actual, wanted = core_ids(), set(items)
    if actual != wanted:
        raise RuntimeError(f"logo definitions differ from core descriptors: missing={sorted(actual-wanted)}, extra={sorted(wanted-actual)}")
    for identity in [*items, *extras]:
        d = asset_dir(identity)
        d.mkdir(parents=True, exist_ok=True)
        small = pixel_icon(identity)
        small.save(d / "logo-32.png", optimize=True)
        (d / "icon.rgb565").write_bytes(rgb565(small))
    make_pixel_contact_sheet(display_items(items, extras))


def validate_icon(data: bytes) -> None:
    if len(data) != 32 * 32 * 2:
        raise ValueError(f"RGB565 icon has {len(data)} bytes, expected 2048")


def check() -> None:
    items = definitions()
    extras = extra_definitions()
    expected = core_ids()
    if set(items) != expected:
        raise AssertionError(f"definitions do not match cores: {sorted(set(items) ^ expected)}")
    if set(extras) != {"u64", "firmware", "trx64"}:
        raise AssertionError("the U64, TRX64 and generic firmware standalone marks are not all defined")
    for identity in [*sorted(expected), *extras]:
        d = asset_dir(identity)
        names = {path.name for path in d.glob("*")}
        if names != {"logo-32.png", "icon.rgb565"}:
            raise AssertionError(f"{d.relative_to(ROOT)} holds {sorted(names)}")
        small = Image.open(d / "logo-32.png").convert("RGB")
        if small.size != (32, 32) or list(small.getdata()) != list(pixel_icon(identity).getdata()):
            raise AssertionError(f"{identity}: 32 px PNG differs from its native pixel definition")
        if not set(icon_master(identity).getdata()) <= ICON_COLOURS:
            raise AssertionError(f"{identity}: icon master is not monochrome")
        packed = (d / "icon.rgb565").read_bytes()
        validate_icon(packed)
        if packed != rgb565(small):
            raise AssertionError(f"{identity}: RGB565 derivative does not match the 32 px PNG")
    ultimate = [rgb565(pixel_icon(identity)) for identity in ("trx64", "u64", "c64u")]
    if len(set(ultimate)) != 3:
        raise AssertionError("TRX64, U64 and C64U icon payloads are not distinct")
    shown = display_items(items, extras)
    if Image.open(HERE / "contact-sheet-32.png").size != pixel_contact_sheet_size(len(shown)):
        raise AssertionError("32 px contact sheet has wrong dimensions")
    try:
        validate_icon(bytes(2047))
    except ValueError:
        pass
    else:
        raise AssertionError("negative control: a short RGB565 icon passed")
    print(f"core logos: {len(expected)} core sets plus U64 and the generic firmware mark, native 32 px/RGB565 checks passed; short-file control failed as required")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate generated assets without changing them")
    args = parser.parse_args()
    try:
        if not args.check:
            build()
        check()
    except (AssertionError, OSError, RuntimeError, ValueError) as error:
        print(f"core logos: FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
