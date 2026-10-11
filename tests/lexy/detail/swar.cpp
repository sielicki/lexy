// Copyright (C) 2020-2025 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include <lexy/_detail/swar.hpp>

#include <doctest/doctest.h>

using namespace lexy::_detail;

TEST_CASE("swar_fill")
{
    REQUIRE(sizeof(swar_int) == 8);
    CHECK(swar_fill(char(0)) == 0);
    CHECK(swar_fill(char(0x30)) == 0x30303030'30303030);
    CHECK(swar_fill(char(0x80)) == 0x80808080'80808080);
    CHECK(swar_fill(char(0xAA)) == 0xAAAAAAAA'AAAAAAAA);
    CHECK(swar_fill(char16_t(0x30)) == 0x00300030'00300030);
    CHECK(swar_fill(char16_t(0xAA)) == 0x00AA00AA'00AA00AA);
}

TEST_CASE("swar_fill_compl")
{
    REQUIRE(sizeof(swar_int) == 8);
    CHECK(swar_fill_compl(char(0)) == 0xFFFFFFFF'FFFFFFFF);
    CHECK(swar_fill_compl(char(0xF)) == 0xF0F0F0F0'F0F0F0F0);
    CHECK(swar_fill_compl(char16_t(0xF)) == 0xFFF0FFF0'FFF0FFF0);
}

TEST_CASE("swar_pack")
{
    REQUIRE(sizeof(swar_int) == 8);

    constexpr auto single_char = swar_pack(char(0x11));
    CHECK(single_char.value == 0x11);
    CHECK(single_char.mask == 0xFF);
    CHECK(single_char.count == 1);

    constexpr auto multiple_chars
        = swar_pack(char(0x00), char(0x11), char(0x22), char(0x33), char(0x44));
    CHECK(multiple_chars.value == 0x4433221100);
    CHECK(multiple_chars.mask == 0xFFFFFFFFFF);
    CHECK(multiple_chars.count == 5);

    constexpr auto full = swar_pack(char32_t(0x11), char32_t(0x22));
    CHECK(full.value == 0x00000022'00000011);
    CHECK(full.mask == 0xFFFFFFFF'FFFFFFFF);
    CHECK(full.count == 2);

    constexpr auto overflow = swar_pack(char32_t(0x11), char32_t(0x22), char32_t(0x33));
    CHECK(overflow.value == 0x00000022'00000011);
    CHECK(overflow.mask == 0xFFFFFFFF'FFFFFFFF);
    CHECK(overflow.count == 2);

    constexpr auto offset
        = swar_pack<2>(char(0x00), char(0x11), char(0x22), char(0x33), char(0x44));
    CHECK(offset.value == 0x443322);
    CHECK(offset.mask == 0xFFFFFF);
    CHECK(offset.count == 3);

    constexpr auto overflow_offset = swar_pack<1>(char32_t(0x11), char32_t(0x22), char32_t(0x33));
    CHECK(overflow_offset.value == 0x00000033'00000022);
    CHECK(overflow_offset.mask == 0xFFFFFFFF'FFFFFFFF);
    CHECK(overflow_offset.count == 2);
}

TEST_CASE("swar_find_difference")
{
    REQUIRE(sizeof(swar_int) == 8);
    constexpr auto a   = swar_pack('a').value;
    constexpr auto A   = swar_pack('A').value;
    constexpr auto abc = swar_pack('a', 'b', 'c').value;
    constexpr auto aBc = swar_pack('a', 'B', 'c').value;

    CHECK(swar_find_difference<char>(a, a) == 8);
    CHECK(swar_find_difference<char>(a, A) == 0);
    CHECK(swar_find_difference<char>(abc, aBc) == 1);
    CHECK(swar_find_difference<char>(0x1'FFFF'FFFF, 0x2'FFFF'FFFF) == 4);
}

TEST_CASE("swar_find_prefix")
{
    constexpr auto find
        = [](swar_int word) { return swar_find_prefix<0x20, 0x7E, '"', '\\', 0xFF>(word); };
    static_assert(find(swar_fill('a')) == swar_length<char>);
    CHECK(find(swar_pack('a', '"', '#', 'a', 'a', 'a', 'a', 'a').value) == 1);

    // Check every byte value and position, including carry/borrow between lanes.
    for (auto pos = 0u; pos != swar_length<char>; ++pos)
        for (auto c = 0u; c != 256; ++c)
        {
            auto word = swar_fill('a');
            word &= ~(swar_int(0xFF) << (pos * 8));
            word |= swar_int(c) << (pos * 8);
            auto valid = c >= 0x20 && c <= 0x7E && c != '"' && c != '\\';
            CHECK(find(word) == (valid ? swar_length<char> : pos));
        }

    auto state = swar_int(0x123456789ABCDEF0);
    for (auto i = 0; i != 10000; ++i)
    {
        state         = state * swar_int(6364136223846793005ULL) + 1;
        auto expected = swar_length<char>;
        for (auto pos = 0u; pos != swar_length<char>; ++pos)
        {
            auto c = (state >> (pos * 8)) & 0xFF;
            if (c < 0x20 || c > 0x7E || c == '"' || c == '\\')
            {
                expected = pos;
                break;
            }
        }
        CHECK(find(state) == expected);
    }

    CHECK(swar_find_prefix<0, 127>(swar_fill(char(0))) == swar_length<char>);
    CHECK(swar_find_prefix<0, 127>(swar_fill(char(127))) == swar_length<char>);
    CHECK(swar_find_prefix<0, 127>(swar_fill(char(128))) == 0);
    CHECK(swar_find_prefix<'a', 'a'>(swar_fill('a')) == swar_length<char>);
    CHECK(swar_find_prefix<'a', 'a'>(swar_fill('b')) == 0);
}

TEST_CASE("swar_has_zero")
{
    SUBCASE("char")
    {
        constexpr auto all_zero = swar_fill(char(0));
        CHECK(swar_has_zero<char>(all_zero));

        constexpr auto all_one = swar_fill(char(1));
        CHECK(!swar_has_zero<char>(all_one));

        constexpr auto all_high = swar_fill(char(0xAB));
        CHECK(!swar_has_zero<char>(all_high));

        constexpr auto contains_zero = swar_pack('a', 'b', 'c', char(0), 'd', 'e', 'f', 'g').value;
        CHECK(swar_has_zero<char>(contains_zero));
    }
    SUBCASE("char32_t")
    {
        constexpr auto all_zero = swar_fill(char32_t(0));
        CHECK(swar_has_zero<char32_t>(all_zero));

        constexpr auto all_one = swar_fill(char32_t(1));
        CHECK(!swar_has_zero<char32_t>(all_one));

        constexpr auto all_high = swar_fill(char32_t(0xAB));
        CHECK(!swar_has_zero<char32_t>(all_high));

        constexpr auto contains_zero = swar_pack(char32_t('a'), char32_t(0)).value;
        CHECK(swar_has_zero<char32_t>(contains_zero));
    }
}

TEST_CASE("swar_has_char")
{
    SUBCASE("char")
    {
        constexpr auto all_zero = swar_fill(char(0));
        CHECK(!swar_has_char<char, 1>(all_zero));

        constexpr auto all_one = swar_fill(char(1));
        CHECK(swar_has_char<char, 1>(all_one));

        constexpr auto all_high = swar_fill(char(0xAB));
        CHECK(!swar_has_char<char, 1>(all_high));

        constexpr auto contains_one = swar_pack('a', 'b', 'c', char(1), 'd', 'e', 'f', 'g').value;
        CHECK(swar_has_char<char, 1>(contains_one));
    }
    SUBCASE("char32_t")
    {
        constexpr auto all_zero = swar_fill(char32_t(0));
        CHECK(!swar_has_char<char32_t, 1>(all_zero));

        constexpr auto all_one = swar_fill(char32_t(1));
        CHECK(swar_has_char<char32_t, 1>(all_one));

        constexpr auto all_high = swar_fill(char32_t(0xAB));
        CHECK(!swar_has_char<char32_t, 1>(all_high));

        constexpr auto contains_one = swar_pack(char32_t('a'), char32_t(1)).value;
        CHECK(swar_has_char<char32_t, 1>(contains_one));
    }
}

