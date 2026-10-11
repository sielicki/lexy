// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/ungrammar.cpp" // NOLINT

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

TEST_CASE("ungrammar definitions and precedence")
{
    auto tree = parse("Root = first:Name ('+' Name)* | 'none'\nName = 'ident'\n");
    CHECK(contents<grammar::definition>(tree).size() == 2);
    CHECK(contents<grammar::label>(tree).size() == 1);
    CHECK(contents<grammar::repetition>(tree).size() == 1);
    CHECK(contents<grammar::sequence>(tree).size() == 4);
}
TEST_CASE("ungrammar trivia, escaping, and recursion")
{
    for (auto str : {"", "// comment", "A = B C\n D\nB = 'x'", "A='\\\'' | '\\\\'",
                     "A = ((B?)* | named:Other)", "A = '世界'", "A='a' // tail\n"})
        parse(str);
}
TEST_CASE("ungrammar malformed rules")
{
    for (auto str : {"A=", "A=B|", "A=|B", "A=()", "A='unterminated", "A='bad\\q'", "A=''", "A=(B",
                     "A=B)", "A=label:", "A1 = B", "A=B;", "A=B\nC="})
        fail(str);
}

TEST_CASE("ungrammar definition boundaries across comments")
{
    auto tree = parse("A = B C\n // separate definition\nD // comment before equals\n = E");
    CHECK(contents<grammar::definition>(tree).size() == 2);
    CHECK(contents<grammar::node_rule>(tree).size() == 3);
    fail("A=B\nC=");
    fail("A=B /* unsupported comment */");
}

TEST_CASE("ungrammar complete document")
{
    parse(R"sample(// A grammar describing grammar rules.
Grammar = Node*
Node = name:'ident' '=' Rule
Rule = 'ident' | 'token' | lhs:Rule '|' rhs:Rule | '(' Rule ')' | Rule '*'
)sample");
}
