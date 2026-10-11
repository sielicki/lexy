// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/printf.cpp" // NOLINT

#include <doctest/doctest.h>
#include <lexy/action/match.hpp>
#include <lexy/callback.hpp>
#include <lexy/input/string_input.hpp>
#include <string>
#include <vector>

namespace
{
using tree_type = lexy::parse_tree_for<lexy::string_input<lexy::utf8_encoding>>;

tree_type parse(const char* str)
{
    INFO(str);
    auto      input = lexy::zstring_input<lexy::utf8_encoding>(str);
    tree_type tree;
    REQUIRE(lexy::parse_as_tree<grammar::format_string>(tree, input, lexy::noop));
    std::string restored;
    for (auto [event, node] : tree.traverse())
        if (event == lexy::traverse_event::leaf)
            restored.append(node.lexeme().begin(), node.lexeme().end());
    CHECK(restored == str);
    CHECK(tree.remaining_input().empty());
    return tree;
}

template <typename Production>
std::vector<std::string> contents(const tree_type& tree)
{
    std::vector<std::string> result;
    for (auto [event, node] : tree.traverse())
        if (event == lexy::traverse_event::enter && node.kind() == Production{})
            result.emplace_back(node.covering_lexeme().begin(), node.covering_lexeme().end());
    return result;
}

void fail(const char* str)
{
    INFO(str);
    CHECK(!lexy::match<grammar::format_string>(lexy::zstring_input<lexy::utf8_encoding>(str)));
}
} // namespace

TEST_CASE("printf text and escaped percent")
{
    for (auto str : {"", "ordinary text", " \t\r\n", "café 世界", "100%%", "%%%%", "%%d"})
    {
        auto tree = parse(str);
        CHECK(contents<grammar::format>(tree).empty());
    }
    auto tree = parse("before %d %% %s after\n");
    CHECK(contents<grammar::format>(tree) == std::vector<std::string>{"%d", "%s"});
    auto adjacent = parse("%%%d%%");
    CHECK(contents<grammar::format>(adjacent) == std::vector<std::string>{"%d"});
}

TEST_CASE("printf field boundaries and conversion extensions")
{
    auto tree = parse("%-18.7s");
    CHECK(contents<grammar::flags>(tree) == std::vector<std::string>{"-"});
    CHECK(contents<grammar::width>(tree) == std::vector<std::string>{"18"});
    CHECK(contents<grammar::precision>(tree) == std::vector<std::string>{".7"});
    CHECK(contents<grammar::type>(tree) == std::vector<std::string>{"s"});

    auto wide = parse("%12c%4hc%9C%6lc");
    CHECK(contents<grammar::width>(wide) == std::vector<std::string>{"12", "4", "9", "6"});
    CHECK(contents<grammar::size>(wide) == std::vector<std::string>{"h", "l"});
    CHECK(contents<grammar::type>(wide) == std::vector<std::string>{"c", "c", "C", "c"});

    auto extended = parse("record=%v, quoted=%q, detailed=%#v");
    CHECK(contents<grammar::type>(extended) == std::vector<std::string>{"v", "q", "v"});
    CHECK(contents<grammar::flags>(extended) == std::vector<std::string>{"#"});
}

TEST_CASE("printf flags, dynamic fields, and empty precision")
{
    auto tree = parse("%#08x %-+12.3f %'.f %*.*s %I10d");
    CHECK(contents<grammar::flags>(tree) == std::vector<std::string>{"#0", "-+", "'", "I"});
    CHECK(contents<grammar::width>(tree) == std::vector<std::string>{"8", "12", "*", "10"});
    CHECK(contents<grammar::precision>(tree) == std::vector<std::string>{".3", ".", ".*"});
    CHECK(contents<grammar::type>(tree) == std::vector<std::string>{"x", "f", "f", "s", "d"});
}

TEST_CASE("printf length modifiers use longest matching")
{
    for (auto modifier : {"hh", "h", "ll", "l", "L", "j", "z", "t", "I32", "I64", "I", "w"})
    {
        auto str  = std::string("%") + modifier + "d";
        auto tree = parse(str.c_str());
        CHECK(contents<grammar::size>(tree) == std::vector<std::string>{modifier});
        CHECK(contents<grammar::type>(tree) == std::vector<std::string>{"d"});
        CHECK(contents<grammar::flags>(tree).empty());
    }
    auto tree = parse("%#08I64x");
    CHECK(contents<grammar::size>(tree) == std::vector<std::string>{"I64"});
}

TEST_CASE("printf incomplete and unsupported conversions")
{
    for (auto str : {"%", "trailing %", "%%%", "%12", "%*", "%.", "%.*", "%#-", "%ll", "%I32",
                     "%2$d", "%*3$d", "%1.2.3f", "%_d", "%é", "%\nd"})
        fail(str);
}

TEST_CASE("printf complete document")
{
    parse(R"sample(User: %-18.7s
Count: %#08x; size: %zu; wide count: %I64d
Measurement: %+12.3f; dynamic string: %*.*s
Go-style value: %#v; progress: 100%%
)sample");
}
