// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/pgn.cpp" // NOLINT

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
    INFO(std::string(str));
    auto      input = lexy::zstring_input<lexy::utf8_encoding>(str);
    tree_type tree;
    REQUIRE(lexy::parse_as_tree<grammar::document>(tree, input, lexy::noop));
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
    INFO(std::string(str));
    CHECK(!lexy::match<grammar::document>(lexy::zstring_input<lexy::utf8_encoding>(str)));
}
} // namespace

TEST_CASE("pgn game structure and recursive variations")
{
    auto tree
        = parse("[Event \"demo\"]\n1. e4 {[%clk 0:05:00]} e5 (1... c5 (1... e6)) 2. Nf3 Nc6 1-0\n");
    CHECK(contents<grammar::game>(tree).size() == 1);
    CHECK(contents<grammar::tagpair>(tree).size() == 1);
    CHECK(contents<grammar::variation>(tree).size() == 2);
    CHECK(contents<grammar::san_move>(tree).size() == 6);
    CHECK(contents<grammar::command>(tree).size() == 1);
}
TEST_CASE("pgn notation extensions and comments")
{
    for (auto str :
         {"", "% leading\n; comment\n", "1. e4 e5 *", "[Event \"x\"] 1. d4 d5", "1. g8=N e1Q 0-1",
          "1. 0-0—0 O-O *", "1. ♘f3 ♞f6 ½—½", "1. e4‼ e5 &excl;&excl; 1-0",
          "1. e4 {outer {inner}} e5 *", "1. N@f3 P@e4 1-0", "1. g1-f3 g8-f6 1-0",
          "[Event “quotes”] 0-1", "1. e4 N (1... e5) 1-0\n1. d4 d5 0-1"})
        parse(str);
}
TEST_CASE("pgn malformed notation")
{
    for (auto str : {"[Event \"unclosed]", "[Event \"x\"]", "1. e4", "1. e9 *", "1. Kz4 *",
                     "1. e4 (1... e5 *", "1. e4 {unclosed *", "1. e4 $x *",
                     "1. e4 % not column zero\n*", "  % indented\n1. e4 *", "[% \"x\"] *"})
        fail(str);
}

TEST_CASE("pgn game boundaries and SAN versus LAN")
{
    auto tree = parse("1. d4xc5 a2-a4 *");
    CHECK(contents<grammar::san_move>(tree).size() == 1);
    CHECK(contents<grammar::lan_move>(tree).size() == 1);
    fail("*");
    fail("[Event \"x\"] 1. e4 })");
}

TEST_CASE("pgn complete document")
{
    parse(R"sample([Event "A parser demonstration"]
[White "Lexy"]
[Black "Tree-sitter"]
[Result "1-0"]

1. e4 {[%clk 0:05:00] A classical opening.} e5
2. Nf3 Nc6 (2... Nf6 3. Nxe5) 3. Bb5 a6 $1 1-0
)sample");
}

TEST_CASE("pgn literal glyphs and HTML references")
{
    for (auto result : {"½–½", "&#189;&ndash;&#189;", "&#xBd;&#X2013;&#XbD;",
                        "&frac12;&dash;&half;", "1&#45;0", "1&#X2D;0"})
    {
        auto text = std::string("1. d4 d5 ") + result;
        auto tree = parse(text.c_str());
        CHECK(contents<grammar::result_code>(tree) == std::vector<std::string>{result});
    }
    parse("[Event &quot;Reference spelling&quot;] 1. d4&#33; d5&#x3F; *");
    parse("1. d4 TN d5 (1... Nf6 RR) 2. c4 &Square; e6 *");
    fail("1. d4 d5 &#x110000;");
    fail("1. d4 d5 &#xBD");
    fail("1. d4 d5 &Half;&dash;&half;");
    fail("1. d4 tn d5 *");
}

TEST_CASE("pgn percent comments require column zero without external state")
{
    parse(R"(% first line
% second line
; ordinary comment
% after a semicolon comment
1.
% after a move number
 d4 d5 (1... Nf6
% inside a variation
2. c4) *
)");
    parse("A collection\n----\n% after a section heading\n1. c4 e5 *");
    parse("1. c4\r\n% CRLF comment\r\n e5 *");
    fail("1. c4\n % indented\n e5 *");
    fail("1. c4\r% after a bare CR\n e5 *");
}
