# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow"]
# ///
"""Generates jpeg_test_images.hpp, the JPEG test vectors for the idfxx_jpeg tests.

Run from this directory with uv (needs network access to PyPI the first time):

    uv run gen_jpeg_test_images.py

The images are encoded with Pillow, and each golden hash is the 32-bit FNV-1a
hash of the RGB bytes Pillow decodes (libjpeg-turbo's defaults: islow IDCT and
fancy upsampling). Pillow can only decode at scales 1/1, 1/2, 1/4 and 1/8, so
the hashes for every M/8 output scale come from libjpeg-turbo's djpeg, which
must be on PATH (e.g. Homebrew's jpeg-turbo). The script checks that djpeg and
Pillow agree wherever both can decode, and fails if they do not.

The arithmetic-coded image is the baseline 4:2:0 image transcoded losslessly by
jpegtran, and the 12-bit and lossless images are encoded by cjpeg (libjpeg-turbo
3.x, for -precision 12 and -lossless); all three tools must be on PATH.

The output is committed; the build does not run this script.
"""

import io
import pathlib
import shutil
import subprocess
import sys

from PIL import Image, features

OUTPUT = pathlib.Path(__file__).with_name("jpeg_test_images.hpp")


def fnv1a(data: bytes) -> int:
    h = 0x811C9DC5
    for byte in data:
        h ^= byte
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def pattern(width: int, height: int) -> Image.Image:
    """A deterministic test picture: smooth gradients plus hard edges."""
    img = Image.new("RGB", (width, height))
    px = img.load()
    for y in range(height):
        for x in range(width):
            r = (x * 255) // (width - 1)
            g = (y * 255) // (height - 1)
            b = ((x * 7 + y * 13) * 37) & 0xFF
            if (x // 5 + y // 4) % 2 == 0:
                r, g = 255 - r, 255 - g
            px[x, y] = (r, g, b)
    return img


def encode(img: Image.Image, **kwargs) -> bytes:
    out = io.BytesIO()
    img.save(out, "JPEG", quality=90, optimize=False, **kwargs)
    return out.getvalue()


def pil_rgb(data: bytes, scale: int = 1) -> tuple[int, int, bytes]:
    img = Image.open(io.BytesIO(data))
    if scale != 1:
        img.draft(img.mode, (img.width // scale, img.height // scale))
        assert img.decoderconfig == (scale, 0), img.decoderconfig
    img.load()
    rgb = img.convert("RGB")
    return rgb.width, rgb.height, rgb.tobytes()


def djpeg_rgb(djpeg: str, data: bytes, eighths: int = 8) -> tuple[int, int, bytes]:
    ppm = subprocess.run(
        [djpeg, "-rgb", "-ppm", "-scale", f"{eighths}/8"], input=data, capture_output=True, check=True
    ).stdout
    fields = ppm.split(maxsplit=4)
    assert fields[0] == b"P6" and fields[3] == b"255", fields[:4]
    width, height = int(fields[1]), int(fields[2])
    pixels = fields[4]
    assert len(pixels) == width * height * 3
    return width, height, pixels


def libjpeg_tool(path: str, args: list[str], data: bytes) -> bytes:
    return subprocess.run([path, *args], input=data, capture_output=True, check=True).stdout


def find_marker(data: bytes, marker: int) -> int:
    """The offset of the segment with this marker before the first scan, or -1."""
    i = 2
    while i + 4 <= len(data) and data[i] == 0xFF:
        if data[i + 1] == marker:
            return i
        if data[i + 1] == 0xDA:
            break
        i += 2 + ((data[i + 2] << 8) | data[i + 3])
    return -1


def c_array(name: str, data: bytes) -> str:
    lines = [f"inline constexpr uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i : i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    djpeg, jpegtran, cjpeg = (shutil.which(tool) for tool in ("djpeg", "jpegtran", "cjpeg"))
    if djpeg is None or jpegtran is None or cjpeg is None:
        print("djpeg, jpegtran and cjpeg (libjpeg-turbo) must be on PATH", file=sys.stderr)
        return 1
    print(f"Pillow libjpeg-turbo {features.version('libjpeg_turbo')}", file=sys.stderr)

    small = pattern(37, 23)
    cases = {
        "baseline_420": encode(small, subsampling=2),
        "baseline_444": encode(small, subsampling=0),
        "grayscale": encode(small.convert("L")),
        "progressive": encode(small, subsampling=2, progressive=True),
        "restart": encode(small, subsampling=2, restart_marker_blocks=1),
        # Taller than five MCU rows, so libjpeg's own check against
        # max_memory_to_use applies to its coefficient buffer as well.
        "progressive_tall": encode(pattern(19, 101), subsampling=2, progressive=True),
    }
    cmyk = encode(small.convert("CMYK"))
    scaled = encode(pattern(61, 45), subsampling=2)
    # The smallest useful JPEG, for splicing in hand-built EXIF segments.
    tiny = encode(Image.new("L", (16, 8), 128))

    # The same coefficients as baseline_420, so the same pixels.
    arithmetic = libjpeg_tool(jpegtran, ["-arithmetic"], cases["baseline_420"])
    assert find_marker(arithmetic, 0xC9) >= 0, "jpegtran -arithmetic did not produce an SOF9 image"
    assert find_marker(cases["baseline_420"], 0xC9) < 0
    assert pil_rgb(arithmetic) == pil_rgb(cases["baseline_420"]), "arithmetic and baseline_420 decode differently"
    assert djpeg_rgb(djpeg, arithmetic) == pil_rgb(arithmetic), "djpeg and Pillow disagree on arithmetic"

    # Neither can be decoded: 12-bit is SOF1 with 12-bit samples, lossless is SOF3.
    ppm = io.BytesIO()
    pattern(16, 8).save(ppm, "PPM")
    precision_12 = libjpeg_tool(cjpeg, ["-precision", "12"], ppm.getvalue())
    lossless = libjpeg_tool(cjpeg, ["-lossless", "1"], ppm.getvalue())
    sof1 = find_marker(precision_12, 0xC1)
    assert sof1 >= 0 and precision_12[sof1 + 4] == 12
    assert find_marker(lossless, 0xC3) >= 0

    out = [
        "// SPDX-License-Identifier: Apache-2.0",
        "// Copyright 2026 Chris Leishman",
        "",
        "// Generated by gen_jpeg_test_images.py; do not edit.",
        "",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace jpeg_test_images {",
        "",
        "// clang-format off",
        "",
    ]

    for name, data in cases.items():
        width, height, rgb = pil_rgb(data)
        assert djpeg_rgb(djpeg, data) == (width, height, rgb), f"djpeg and Pillow disagree on {name}"
        out += [
            f"// {name}: {width}x{height}, {len(data)} bytes",
            c_array(name, data),
            f"inline constexpr size_t {name}_width = {width};",
            f"inline constexpr size_t {name}_height = {height};",
            f"inline constexpr uint32_t {name}_hash = 0x{fnv1a(rgb):08x};",
            "",
        ]

    out += [
        f"// cmyk: {small.width}x{small.height} CMYK (Adobe), {len(cmyk)} bytes",
        c_array("cmyk", cmyk),
        "",
    ]

    out += [
        f"// scaled: 61x45 baseline 4:2:0, {len(scaled)} bytes, with the output at each scale M/8",
        c_array("scaled", scaled),
        "",
        "struct scaled_output {",
        "    size_t width;",
        "    size_t height;",
        "    uint32_t hash;",
        "};",
        "",
        "// Indexed by M - 1.",
        "inline constexpr scaled_output scaled_outputs[8] = {",
    ]
    for eighths in range(1, 9):
        width, height, rgb = djpeg_rgb(djpeg, scaled, eighths)
        if 8 % eighths == 0:
            assert pil_rgb(scaled, 8 // eighths) == (width, height, rgb), f"djpeg and Pillow disagree at {eighths}/8"
        out.append(f"    {{{width}, {height}, 0x{fnv1a(rgb):08x}}}, // {eighths}/8")
    out += ["};", ""]

    out += [
        f"// tiny: 16x8 grayscale, {len(tiny)} bytes",
        c_array("tiny", tiny),
        "",
        f"// arithmetic: baseline_420 transcoded to arithmetic coding (SOF9), {len(arithmetic)} bytes;",
        "// it decodes to baseline_420_hash",
        c_array("arithmetic", arithmetic),
        "",
        f"// precision_12: 16x8 with 12-bit samples, {len(precision_12)} bytes",
        c_array("precision_12", precision_12),
        "",
        f"// lossless: 16x8 lossless (SOF3), {len(lossless)} bytes",
        c_array("lossless", lossless),
        "",
        "// clang-format on",
        "",
        "} // namespace jpeg_test_images",
        "",
    ]

    OUTPUT.write_text("\n".join(out))
    print(f"wrote {OUTPUT}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
