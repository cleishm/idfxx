#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Rasterize a TrueType/OpenType font into an idfxx::font::proportional_font C++ source file.

Usage:
    ttf_to_c.py FONT IDENTIFIER --size PX [-o OUT.cpp]
        [--charset ascii|latin1|cp1252] [--chars TEXT] [--ranges SPEC]
        [--face-index N] [--header H] [--namespace NS] [--replacement CP|none]
        [--render mono|threshold] [--threshold 1-255]
        [--preview OUT.png [--preview-text TEXT] [--preview-scale N]]

FONT is any outline font FreeType reads (.ttf, .otf, .ttc). PX is the em size in pixels
(FT_Set_Pixel_Sizes(face, 0, PX)), so glyph heights depend on the font's design: capitals are
typically around 0.7 PX tall.

The glyph set is the union of the --charset (default cp1252: printable ASCII, Latin-1, and the
printable characters Windows-1252 places in 0x80-0x9F), the code points in --chars, the --ranges
(a comma-separated list of code points and inclusive ranges, e.g. "0x100-0x17F,0x2190"), and the
replacement glyph. Requested code points the face lacks are omitted with a warning. A requested
U+00AD SOFT HYPHEN is always emitted as an invisible, zero-width glyph (text rendering never breaks
lines, so the soft hyphen never shows).

The replacement glyph (drawn for code points the font does not cover) defaults to U+FFFD when the
face has it, else '?'. Pass a single character, U+XXXX or 0xXXXX to choose another, or "none" to
skip uncovered code points instead.

Rendering:
    mono       (default) Full hinting for monochrome output, rendered 1-bit by FreeType, whose
               dropout control keeps thin strokes and hairlines intact.
    threshold  Light hinting, 8-bit coverage; a pixel is inked when its coverage is at least
               --threshold (default 128). Lower thresholds embolden a face, higher ones lighten
               it, but thin strokes may break up or vanish.

The generated file defines IDENTIFIER in namespace NS (default "fonts"; may be nested, e.g.
"idfxx::font"). Without --header it includes <idfxx/font.hpp> and declares IDENTIFIER extern itself;
with --header H it includes "H" instead, which must declare
`extern const idfxx::font::proportional_font IDENTIFIER;` in NS.

--preview writes a 1-bit PNG of --preview-text (lines separated by newlines or a literal "\\n")
drawn from the generated tables, laid out as idfxx::gfx::draw_text does, magnified by
--preview-scale.

Requires freetype-py (pip install freetype-py).
"""

import argparse
import ctypes
import os
import re
import shlex
import struct
import sys
import textwrap
import unicodedata
import zlib

try:
    import freetype
except (ImportError, RuntimeError, OSError) as e:
    sys.exit(
        f"error: cannot import freetype ({e}); install freetype-py into this Python: "
        f"{sys.executable} -m pip install freetype-py"
    )

UINT16_MAX = 0xFFFF
UINT32_MAX = 0xFFFFFFFF
INT16_MIN = -0x8000
INT16_MAX = 0x7FFF
MAX_CODE_POINT = 0x10FFFF

IDENTIFIER_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
NAMESPACE_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*\Z")

DEFAULT_PREVIEW_TEXT = (
    "The quick brown fox jumps over the lazy dog\n"
    "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\n"
    "Cr\u00e8me br\u00fbl\u00e9e \u2014 \u201cna\u00efve\u201d caf\u00e9\u2026 12:45"
)

# ASCII stand-ins for punctuation common in font name tables.
ASCII_FOLD = {
    "\u00a9": "(c)",
    "\u00ae": "(R)",
    "\u2122": "(TM)",
    "\u2018": "'",
    "\u2019": "'",
    "\u201c": '"',
    "\u201d": '"',
    "\u2013": "-",
    "\u2014": "-",
    "\u2026": "...",
}


class GenError(Exception):
    """A user-facing generation error."""


SOFT_HYPHEN = 0xAD


class Glyph:
    """One rasterized glyph: packed, trimmed bitmap rows plus placement."""

    def __init__(self, cp, advance, x_offset, y_offset, width, height, data):
        self.cp = cp
        self.advance = advance
        self.x_offset = x_offset
        self.y_offset = y_offset
        self.width = width
        self.height = height
        self.data = data  # height * ceil(width / 8) bytes
        self.bitmap_offset = 0


# --------------------------------------------------------------------------------------------
# Argument parsing helpers


def parse_code_point(text):
    """Parses U+XXXX, 0xXXXX or decimal into a code point."""
    t = text.strip()
    try:
        if t[:2].upper() == "U+":
            cp = int(t[2:], 16)
        else:
            cp = int(t, 0)
    except ValueError:
        raise GenError(f"invalid code point '{text}'") from None
    check_code_point(cp, text)
    return cp


def check_code_point(cp, what):
    if cp < 0 or cp > MAX_CODE_POINT:
        raise GenError(f"code point {what} is outside U+0000-U+10FFFF")
    if 0xD800 <= cp <= 0xDFFF:
        raise GenError(f"code point {what} is a surrogate, not a character")


def parse_ranges(spec):
    """Parses "0x100-0x17F,0x2190" into a set of code points."""
    cps = set()
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part[1:]:
            split = part.index("-", 1)
            lo = parse_code_point(part[:split])
            hi = parse_code_point(part[split + 1 :])
            if hi < lo:
                raise GenError(f"range '{part}' is reversed")
            if lo <= 0xDFFF and hi >= 0xD800:
                raise GenError(f"range '{part}' includes surrogates (U+D800-U+DFFF)")
            cps.update(range(lo, hi + 1))
        else:
            cps.add(parse_code_point(part))
    return cps


def parse_replacement(text):
    """Returns None for "none", else the replacement code point."""
    if text.lower() == "none":
        return None
    if len(text) == 1:
        cp = ord(text)
        check_code_point(cp, f"U+{cp:04X}")
        return cp
    if text[:2].upper() not in ("U+", "0X"):
        raise GenError(
            f"invalid --replacement '{text}': use a single character, U+XXXX, 0xXXXX or none"
        )
    return parse_code_point(text)


def decode_arg(text):
    """Recovers a command-line argument's text as UTF-8, independent of the locale."""
    try:
        return os.fsencode(text).decode("utf-8")
    except UnicodeDecodeError:
        raise GenError("command-line text is not valid UTF-8") from None


def charset_code_points(name):
    cps = set(range(0x20, 0x7F))
    if name in ("latin1", "cp1252"):
        cps.update(range(0xA0, 0x100))
    if name == "cp1252":
        for b in range(0x80, 0xA0):
            try:
                ch = bytes([b]).decode("cp1252")
            except UnicodeDecodeError:
                continue  # unassigned in Windows-1252
            if not unicodedata.category(ch).startswith("C"):
                cps.add(ord(ch))
    return cps


def format_ranges(cps):
    """Formats a set of code points as a canonical --ranges spec."""
    parts = []
    for first, last in coalesce(sorted(cps)):
        parts.append(f"0x{first:04X}" if first == last else f"0x{first:04X}-0x{last:04X}")
    return ",".join(parts)


def coalesce(sorted_cps):
    """Yields (first, last) runs of consecutive code points."""
    runs = []
    for cp in sorted_cps:
        if runs and runs[-1][1] + 1 == cp:
            runs[-1][1] = cp
        else:
            runs.append([cp, cp])
    return [(a, b) for a, b in runs]


# --------------------------------------------------------------------------------------------
# Text helpers


def to_ascii(text):
    """Folds text to printable ASCII with whitespace collapsed to single spaces."""
    out = []
    for ch in text:
        if ch in ASCII_FOLD:
            out.append(ASCII_FOLD[ch])
            continue
        for d in unicodedata.normalize("NFKD", ch):
            if unicodedata.combining(d):
                continue
            if d.isspace():
                out.append(" ")
            elif 0x20 <= ord(d) <= 0x7E:
                out.append(d)
            else:
                out.append("?")
    return " ".join("".join(out).split())


def comment_lines(text, prefix="// ", indent="// "):
    """Wraps text into comment lines no wider than 100 columns."""
    lines = textwrap.wrap(
        text,
        width=100,
        initial_indent=prefix,
        subsequent_indent=indent,
        break_long_words=False,
        break_on_hyphens=False,
    )
    # A comment line ending in a backslash would splice the next line into the comment.
    return [line[:-1] + "backslash" if line.endswith("\\") else line for line in lines]


def label(cp):
    """Comment label for a code point."""
    if cp == 0x5C:
        return "U+005C 'backslash'"
    if 0x21 <= cp <= 0x7E:
        return f"U+{cp:04X} '{chr(cp)}'"
    name = unicodedata.name(chr(cp), "")
    return f"U+{cp:04X} {name}" if name else f"U+{cp:04X}"


# --------------------------------------------------------------------------------------------
# Font access


def open_face(path, face_index, size):
    try:
        face = freetype.Face(path, face_index)
    except freetype.FT_Exception as e:
        raise GenError(f"cannot open face {face_index} of {path}: {e}") from None
    if not face.is_scalable:
        raise GenError(f"{path} is not a scalable outline font")
    try:
        face.select_charmap(freetype.FT_ENCODING_UNICODE)
    except freetype.FT_Exception:
        raise GenError(f"{path} has no Unicode character map") from None
    try:
        face.set_pixel_sizes(0, size)
    except freetype.FT_Exception as e:
        raise GenError(f"cannot set {path} to {size} px: {e}") from None
    return face


# Name-table records quoted in the generated banner: version, copyright, license, license URL.
NAME_IDS = (5, 0, 13, 14)


def font_names(face):
    """Returns the version, copyright, license and license URL name records, folded to ASCII.

    A record already quoted in full by an earlier one (typically the license URL inside the license
    description) is left out.
    """

    def rank(n):
        if n.platform_id == 3 and n.language_id == 0x409:
            return 0
        if n.platform_id == 3:
            return 1
        if n.platform_id == 0:
            return 2
        if n.platform_id == 1 and n.encoding_id == 0 and n.language_id == 0:
            return 3
        return None

    best = {}
    for i in range(face.sfnt_name_count):
        n = face.get_sfnt_name(i)
        if n.name_id not in NAME_IDS:
            continue
        r = rank(n)
        if r is None or (n.name_id in best and best[n.name_id][0] <= r):
            continue
        if n.platform_id == 1:
            text = n.string.decode("mac_roman", "replace")
        else:
            text = n.string.decode("utf-16-be", "replace")
        best[n.name_id] = (r, text)
    names = []
    for name_id in NAME_IDS:
        if name_id in best:
            text = to_ascii(best[name_id][1])
            if text and not any(text in earlier for earlier in names):
                names.append(text)
    return names


def decode_name(value):
    if isinstance(value, bytes):
        return value.decode("utf-8", "replace")
    return value or ""


def rasterize(face, cp, render, threshold):
    """Renders the glyph for cp, returning (Glyph, outline_had_ink_but_bitmap_is_blank)."""
    index = face.get_char_index(cp)
    if render == "mono":
        flags = freetype.FT_LOAD_TARGET_MONO | freetype.FT_LOAD_NO_BITMAP
        mode = freetype.FT_RENDER_MODE_MONO
        pixel_mode = freetype.FT_PIXEL_MODE_MONO
    else:
        flags = freetype.FT_LOAD_TARGET_LIGHT | freetype.FT_LOAD_NO_BITMAP
        mode = freetype.FT_RENDER_MODE_NORMAL
        pixel_mode = freetype.FT_PIXEL_MODE_GRAY
    try:
        face.load_glyph(index, flags)
        slot = face.glyph
        has_outline = slot.format == freetype.FT_GLYPH_FORMAT_OUTLINE
        has_contours = has_outline and slot.outline.n_contours > 0
        advance = (slot.advance.x + 32) >> 6
        slot.render(mode)
    except freetype.FT_Exception as e:
        raise GenError(f"cannot render {label(cp)}: {e}") from None

    bm = slot.bitmap
    rows, width, pitch = bm.rows, bm.width, bm.pitch
    if rows and width and bm.pixel_mode != pixel_mode:
        raise GenError(f"{label(cp)} rendered with unexpected pixel mode {bm.pixel_mode}")
    stride = abs(pitch)
    data = ctypes.string_at(bm._FT_Bitmap.buffer, stride * rows) if rows and width else b""

    ink = []
    for r in range(rows):
        # A negative pitch means the rows are stored bottom-up.
        base = (r if pitch > 0 else rows - 1 - r) * stride
        if render == "mono":
            ink.append([bool(data[base + c // 8] & (0x80 >> (c % 8))) for c in range(width)])
        else:
            ink.append([data[base + c] >= threshold for c in range(width)])

    inked_rows = [r for r in range(rows) if any(ink[r])]
    if not inked_rows:
        return Glyph(cp, advance, 0, 0, 0, 0, b""), has_contours
    top, bottom = inked_rows[0], inked_rows[-1]
    inked_cols = [c for c in range(width) if any(ink[r][c] for r in range(top, bottom + 1))]
    left, right = inked_cols[0], inked_cols[-1]

    w = right - left + 1
    h = bottom - top + 1
    bpr = (w + 7) // 8
    packed = bytearray(bpr * h)
    for r in range(h):
        row = ink[top + r]
        for c in range(w):
            if row[left + c]:
                packed[r * bpr + c // 8] |= 0x80 >> (c % 8)
    glyph = Glyph(cp, advance, slot.bitmap_left + left, top - slot.bitmap_top, w, h, bytes(packed))
    return glyph, False


def check_range(value, lo, hi, what):
    if value < lo or value > hi:
        raise GenError(f"{what} = {value} does not fit the font format (range {lo}..{hi})")


# --------------------------------------------------------------------------------------------
# Font building


class Font:
    def __init__(self, ascent, descent, line_height, glyphs, replacement_cp):
        self.ascent = ascent
        self.descent = descent
        self.line_height = line_height
        self.glyphs = glyphs  # sorted by code point
        self.index = {g.cp: i for i, g in enumerate(glyphs)}
        self.replacement = None if replacement_cp is None else self.index[replacement_cp]

        # Ranges of consecutive code points, each pointing at its first glyph.
        self.ranges = []
        for first, last in coalesce([g.cp for g in glyphs]):
            self.ranges.append((first, last, self.index[first]))

        # Pack bitmaps, sharing identical ones.
        self.bitmap = bytearray()
        self.first_use = {}  # bitmap offset -> code point of the glyph that stored it
        offsets = {}
        for g in glyphs:
            if not g.data:
                g.bitmap_offset = 0
                continue
            if g.data in offsets:
                g.bitmap_offset = offsets[g.data]
                continue
            g.bitmap_offset = len(self.bitmap)
            offsets[g.data] = g.bitmap_offset
            self.first_use[g.bitmap_offset] = g.cp
            self.bitmap += g.data

        self._validate()

    def _validate(self):
        check_range(self.ascent, 0, UINT16_MAX, "ascent")
        check_range(self.descent, 0, UINT16_MAX, "descent")
        check_range(self.line_height, 0, UINT16_MAX, "line_height")
        for g in self.glyphs:
            what = label(g.cp)
            check_range(g.bitmap_offset, 0, UINT32_MAX, f"{what} bitmap_offset")
            check_range(g.width, 0, UINT16_MAX, f"{what} width")
            check_range(g.height, 0, UINT16_MAX, f"{what} height")
            check_range(g.advance, 0, UINT16_MAX, f"{what} advance")
            check_range(g.x_offset, INT16_MIN, INT16_MAX, f"{what} x_offset")
            check_range(g.y_offset, INT16_MIN, INT16_MAX, f"{what} y_offset")
        check_range(len(self.bitmap), 0, UINT32_MAX, "bitmap size")
        for first, last, first_glyph in self.ranges:
            check_range(first_glyph, 0, UINT32_MAX, f"U+{first:04X} range first_glyph")

    # Mirrors proportional_font::find / resolve.
    def find(self, cp):
        lo, hi = 0, len(self.ranges)
        while lo < hi:
            mid = lo + (hi - lo) // 2
            first, last, first_glyph = self.ranges[mid]
            if cp < first:
                hi = mid
            elif cp > last:
                lo = mid + 1
            else:
                return self.glyphs[first_glyph + (cp - first)]
        return None

    def resolve(self, cp):
        g = self.find(cp)
        if g is None and self.replacement is not None:
            g = self.glyphs[self.replacement]
        return g

    # Mirrors proportional_font::ink_at.
    def ink_at(self, g, col, row):
        bpr = (g.width + 7) // 8
        return bool(self.bitmap[g.bitmap_offset + row * bpr + col // 8] & (0x80 >> (col % 8)))


def build_font(face, cps, render, threshold):
    glyphs = []
    blank = []
    for cp in sorted(cps):
        g, lost_ink = rasterize(face, cp, render, threshold)
        glyphs.append(g)
        if lost_ink:
            blank.append(cp)
    if blank:
        print(
            f"warning: these glyphs have outlines but rendered no ink ({render}"
            + (f" {threshold}" if render == "threshold" else "")
            + "): "
            + ", ".join(label(cp) for cp in blank),
            file=sys.stderr,
        )
    m = face.size
    ascent = -((-m.ascender) // 64)  # ceil
    descent = -(m.descender // 64)  # ceil(-descender / 64)
    line_height = max((m.height + 32) >> 6, ascent + descent)
    return glyphs, ascent, descent, line_height


# --------------------------------------------------------------------------------------------
# Output


def emit(font, args, face, names):
    ident = args.identifier
    out = []
    family = to_ascii(decode_name(face.family_name))
    style = to_ascii(decode_name(face.style_name))
    font_file = to_ascii(os.path.basename(args.font))
    render_desc = "mono" if args.render == "mono" else f"threshold {args.threshold}"

    out.append("// Generated by scripts/ttf_to_c.py - DO NOT EDIT.")
    out.extend(
        comment_lines(
            f"Source: {font_file} ({family} {style}".rstrip() + f"), {args.size} px, {render_desc}",
            indent="//     ",
        )
    )
    for text in names:
        out.extend(comment_lines(text, indent="//   "))
    out.append("//")
    out.extend(comment_lines(" ".join(regenerate_args(args)), "// regenerate: ", "//     "))
    out.append("")
    if args.header is None:
        out.append("#include <idfxx/font.hpp>")
    else:
        out.append(f'#include "{args.header}"')
    out.append("")
    out.append(f"namespace {args.namespace} {{")
    out.append("")
    if args.header is None:
        out.append(f"extern const idfxx::font::proportional_font {ident};")
        out.append("")
    out.append("namespace {")
    out.append("")
    out.append("// clang-format off")
    out.append(f"constexpr uint8_t {ident}_bitmap[] = {{")
    if not font.bitmap:
        out.append("    0x00, // no glyph has ink")
    for g in font.glyphs:
        if not g.data or font.first_use.get(g.bitmap_offset) != g.cp:
            continue
        out.append(f"    // {label(g.cp)}")
        bpr = (g.width + 7) // 8
        for r in range(g.height):
            row = g.data[r * bpr : (r + 1) * bpr]
            for i in range(0, len(row), 16):
                out.append("    " + ", ".join(f"0x{b:02X}" for b in row[i : i + 16]) + ",")
    out.append("};")
    out.append("")
    out.append(f"constexpr idfxx::font::glyph_metrics {ident}_glyphs[] = {{")
    for g in font.glyphs:
        fields = (g.bitmap_offset, g.width, g.height, g.advance, g.x_offset, g.y_offset)
        out.append("    {" + ", ".join(str(v) for v in fields) + f"}}, // {label(g.cp)}")
    out.append("};")
    out.append("")
    out.append(f"constexpr idfxx::font::glyph_range {ident}_ranges[] = {{")
    for first, last, first_glyph in font.ranges:
        out.append(f"    {{0x{first:04X}, 0x{last:04X}, {first_glyph}}},")
    out.append("};")
    out.append("// clang-format on")
    out.append("")
    out.append("} // namespace")
    out.append("")
    out.append(f"const idfxx::font::proportional_font {ident}{{")
    out.append(f"    .ascent = {font.ascent},")
    out.append(f"    .descent = {font.descent},")
    out.append(f"    .line_height = {font.line_height},")
    out.append(f"    .ranges = {ident}_ranges,")
    out.append(f"    .glyphs = {ident}_glyphs,")
    if font.replacement is None:
        out.append("    .replacement = nullptr,")
    else:
        out.append(f"    .replacement = &{ident}_glyphs[{font.replacement}],")
    out.append(f"    .bitmap = {ident}_bitmap,")
    out.append("};")
    out.append("")
    out.append(f"}} // namespace {args.namespace}")
    text = "\n".join(out) + "\n"

    if not text.isascii():
        raise GenError("internal error: generated output is not ASCII")
    if any(line.endswith("\\") for line in out):
        raise GenError("internal error: generated line ends in a backslash")
    return text


def regenerate_args(args):
    """The effective, data-affecting arguments, as an ASCII command line."""
    a = ["ttf_to_c.py", shlex.quote(to_ascii(os.path.basename(args.font))), args.identifier]
    a += ["--size", str(args.size), "--charset", args.charset]
    extras = args.extra_cps - charset_code_points(args.charset)
    if extras:
        a += ["--ranges", format_ranges(extras)]
    if args.face_index:
        a += ["--face-index", str(args.face_index)]
    if args.header is not None:
        a += ["--header", shlex.quote(args.header)]
    a += ["--namespace", args.namespace]
    rcp = args.replacement_cp
    a += ["--replacement", "none" if rcp is None else f"U+{rcp:04X}"]
    a += ["--render", args.render]
    if args.render == "threshold":
        a += ["--threshold", str(args.threshold)]
    return a


# --------------------------------------------------------------------------------------------
# Preview


def render_preview(font, text, scale, path):
    margin = 4
    ink = set()
    max_pen = 0
    lines = text.split("\n")
    for i, line in enumerate(lines):
        top = margin + i * font.line_height
        baseline = top + font.ascent
        pen = margin
        for cp in (ord(ch) for ch in line):
            g = font.resolve(cp)
            if g is None:
                continue
            x0 = pen + g.x_offset
            y0 = baseline + g.y_offset
            for row in range(g.height):
                for col in range(g.width):
                    if font.ink_at(g, col, row):
                        ink.add((x0 + col, y0 + row))
            pen += g.advance
        max_pen = max(max_pen, pen)

    width = max_pen + margin
    height = margin + (len(lines) - 1) * font.line_height + font.ascent + font.descent + margin
    dx = dy = 0
    if ink:
        dx = max(0, -min(x for x, _ in ink))
        dy = max(0, -min(y for _, y in ink))
        width = max(width, max(x for x, _ in ink) + 1 + margin)
        height = max(height, max(y for _, y in ink) + 1 + margin)
    width += dx
    height += dy

    rows = [[False] * width for _ in range(height)]
    for x, y in ink:
        rows[y + dy][x + dx] = True
    write_png(path, rows, scale)


def write_png(path, rows, scale):
    """Writes black (True) on white pixels as a 1-bit grayscale PNG."""
    height = len(rows) * scale
    width = len(rows[0]) * scale
    raw = bytearray()
    for row in rows:
        line = bytearray([0])  # filter type None
        acc = 0
        n = 0
        for black in row:
            for _ in range(scale):
                acc = (acc << 1) | (0 if black else 1)
                n += 1
                if n == 8:
                    line.append(acc)
                    acc = n = 0
        if n:
            line.append(((acc << (8 - n)) | ((1 << (8 - n)) - 1)) & 0xFF)
        raw += bytes(line) * scale

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 1, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


# --------------------------------------------------------------------------------------------
# Main


def parse_args(argv):
    p = argparse.ArgumentParser(
        description="Rasterize a TrueType/OpenType font into an "
        "idfxx::font::proportional_font C++ source file.",
        epilog="See the module docstring (top of this script) for details.",
    )
    p.add_argument("font", metavar="FONT", help="font file (.ttf, .otf, .ttc)")
    p.add_argument("identifier", metavar="IDENTIFIER", help="C++ name of the generated font")
    p.add_argument(
        "--size",
        type=int,
        required=True,
        metavar="PX",
        help="em size in pixels (FT_Set_Pixel_Sizes)",
    )
    p.add_argument("-o", "--output", metavar="OUT.cpp", help="output file (default: stdout)")
    p.add_argument(
        "--charset",
        choices=("ascii", "latin1", "cp1252"),
        default="cp1252",
        help="base character set (default: cp1252)",
    )
    p.add_argument("--chars", default="", metavar="TEXT", help="extra characters to include")
    p.add_argument(
        "--ranges", default="", metavar="SPEC", help='extra code points, e.g. "0x100-0x17F,0x2190"'
    )
    p.add_argument("--face-index", type=int, default=0, metavar="N", help="face in a collection")
    p.add_argument(
        "--header",
        metavar="H",
        help='header to include instead of <idfxx/font.hpp>; must declare the font extern',
    )
    p.add_argument("--namespace", default="fonts", metavar="NS", help='namespace (default: fonts)')
    p.add_argument(
        "--replacement",
        metavar="CP|none",
        help="glyph for uncovered code points (default: U+FFFD if present, else '?')",
    )
    p.add_argument("--render", choices=("mono", "threshold"), default="mono")
    p.add_argument(
        "--threshold",
        type=int,
        default=128,
        metavar="1-255",
        help="coverage threshold for --render threshold (default 128)",
    )
    p.add_argument("--preview", metavar="OUT.png", help="also write a PNG preview")
    p.add_argument("--preview-text", metavar="TEXT", help="preview text (\\n separates lines)")
    p.add_argument(
        "--preview-scale", type=int, default=1, metavar="N", help="preview magnification"
    )
    args = p.parse_args(argv)

    if not IDENTIFIER_RE.match(args.identifier):
        p.error(f"IDENTIFIER '{args.identifier}' is not a valid C++ identifier")
    if not NAMESPACE_RE.match(args.namespace):
        p.error(f"--namespace '{args.namespace}' is not a valid C++ namespace name")
    if args.size < 1:
        p.error("--size must be at least 1")
    if args.face_index < 0:
        p.error("--face-index must not be negative")
    if not 1 <= args.threshold <= 255:
        p.error("--threshold must be in 1-255")
    if args.preview is None and (args.preview_text is not None or args.preview_scale != 1):
        p.error("--preview-text and --preview-scale require --preview")
    if args.preview_scale < 1:
        p.error("--preview-scale must be at least 1")
    if args.header is not None and ('"' in args.header or "\n" in args.header):
        p.error("--header must not contain quotes or newlines")
    return args


def run(args):
    chars = decode_arg(args.chars)
    extra = set()
    for ch in chars:
        check_code_point(ord(ch), f"U+{ord(ch):04X} in --chars")
        extra.add(ord(ch))
    extra |= parse_ranges(args.ranges)
    args.extra_cps = extra

    face = open_face(args.font, args.face_index, args.size)

    if args.replacement is None:
        replacement = 0xFFFD if face.get_char_index(0xFFFD) else ord("?")
    else:
        replacement = parse_replacement(decode_arg(args.replacement))
    if replacement is not None and not face.get_char_index(replacement):
        raise GenError(f"the face has no glyph for the replacement {label(replacement)}")
    args.replacement_cp = replacement

    requested = charset_code_points(args.charset) | extra
    invisible = requested & {SOFT_HYPHEN}
    requested -= invisible
    if replacement is not None:
        requested.add(replacement)
    missing = sorted(cp for cp in requested if not face.get_char_index(cp))
    if missing:
        print(
            f"warning: {os.path.basename(args.font)} lacks {len(missing)} requested code "
            "point(s), omitted: " + ", ".join(f"U+{cp:04X}" for cp in missing),
            file=sys.stderr,
        )
    cps = requested.difference(missing)
    if not cps:
        raise GenError("the face has none of the requested code points")

    glyphs, ascent, descent, line_height = build_font(face, cps, args.render, args.threshold)
    glyphs += [Glyph(cp, 0, 0, 0, 0, 0, b"") for cp in invisible - cps]
    glyphs.sort(key=lambda g: g.cp)
    font = Font(ascent, descent, line_height, glyphs, replacement)
    text = emit(font, args, face, font_names(face))

    if args.preview is not None:
        preview_text = DEFAULT_PREVIEW_TEXT
        if args.preview_text is not None:
            preview_text = decode_arg(args.preview_text).replace("\\n", "\n")
        render_preview(font, preview_text, args.preview_scale, args.preview)

    if args.output is None:
        sys.stdout.write(text)
    else:
        with open(args.output, "w", encoding="ascii", newline="\n") as f:
            f.write(text)


def main(argv=None):
    args = parse_args(argv)
    try:
        run(args)
    except GenError as e:
        sys.exit(f"error: {e}")
    except OSError as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
