// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Chris Leishman

// Unit tests for idfxx_font_spleen
// Pure font-data checks — runs everywhere, including QEMU.

#include "idfxx/font/spleen"
#include "unity.h"

#include <cstddef>

using namespace idfxx::font;

namespace {

// Returns true if any pixel of the glyph for c is inked.
bool has_ink(const mono_font& font, char c) {
    const uint8_t* glyph = font.glyph(c);
    for (size_t i = 0; i < font.bytes_per_glyph(); ++i) {
        if (glyph[i] != 0) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("spleen fonts have the documented geometry", "[idfxx][font]") {
    TEST_ASSERT_EQUAL(5, spleen_5x8.width);
    TEST_ASSERT_EQUAL(8, spleen_5x8.height);
    TEST_ASSERT_EQUAL(8, spleen_8x16.width);
    TEST_ASSERT_EQUAL(16, spleen_8x16.height);
}

TEST_CASE("spleen fonts cover printable ASCII", "[idfxx][font]") {
    for (const mono_font* font : {&spleen_5x8, &spleen_8x16}) {
        TEST_ASSERT_EQUAL(' ', font->first_char);
        TEST_ASSERT_EQUAL('~', font->last_char);
        TEST_ASSERT_TRUE(font->contains('A'));
        TEST_ASSERT_FALSE(font->contains('\n'));
    }
}

TEST_CASE("spleen glyphs have ink where expected", "[idfxx][font]") {
    for (const mono_font* font : {&spleen_5x8, &spleen_8x16}) {
        TEST_ASSERT_FALSE(has_ink(*font, ' '));
        for (char c = '!'; c <= '~'; ++c) {
            TEST_ASSERT_TRUE_MESSAGE(has_ink(*font, c), "printable glyph unexpectedly blank");
        }
    }
}
