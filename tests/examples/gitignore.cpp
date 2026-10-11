// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/gitignore.cpp" // NOLINT

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

TEST_CASE("gitignore pattern structure")
{
    auto tree = parse("# comment\n!/build/**/file?.[ch]\n");
    CHECK(contents<grammar::comment>(tree).size() == 1);
    CHECK(contents<grammar::negation>(tree).size() == 1);
    CHECK(contents<grammar::directory_separator>(tree).size() == 3);
    CHECK(contents<grammar::wildcard_chars_allow_slash>(tree).size() == 1);
    CHECK(contents<grammar::bracket_expr>(tree).size() == 1);
}
TEST_CASE("gitignore character classes and escaped patterns")
{
    auto tree = parse("[a-z][[:digit:]][!abc][]]\n\\#literal\\ name\n");
    CHECK(contents<grammar::bracket_expr>(tree).size() == 4);
    CHECK(contents<grammar::bracket_range>(tree).size() == 1);
    CHECK(contents<grammar::bracket_char_class>(tree).size() == 1);
    CHECK(contents<grammar::pattern_char_escaped>(tree).size() == 2);
    for (auto str :
         {"", "\n", "# final comment", "*.o", "café/世界\r\n", "[unfinished\n", "abc\\", "  \n"})
        parse(str);
}
TEST_CASE("gitignore invalid path patterns")
{
    for (auto str : {"/\n", "!/\n", "/", "\xC3\n"})
        fail(str);
}

TEST_CASE("gitignore line boundaries")
{
    auto tree = parse("*.o\r\n!keep.o");
    CHECK(contents<grammar::pattern>(tree) == std::vector<std::string>{"*.o", "!keep.o"});
    fail("a//b\n");
    fail("//a\n");
}

TEST_CASE("gitignore complete document")
{
    parse(R"sample(# Build artifacts, with an exception
/build/
**/*.o
!tests/fixtures/*.o
logs/[0-9][0-9]/*.log
\#literal-name
)sample");
}
