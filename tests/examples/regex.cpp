// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/regex.cpp" // NOLINT

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

TEST_CASE("regex precedence and captures")
{
    auto tree = parse("^(?<name>[a-z]+)(?:x|y){2,4}?\\k<name>$");
    CHECK(contents<grammar::named_capturing_group>(tree).size() == 1);
    CHECK(contents<grammar::non_capturing_group>(tree).size() == 1);
    CHECK(contents<grammar::class_range>(tree).size() == 1);
    CHECK(contents<grammar::quantifier>(tree).size() == 2);
    CHECK(contents<grammar::backreference>(tree).size() == 1);
}
TEST_CASE("regex groups, escapes, and empty alternatives")
{
    for (auto str : {"", "()", "a|", "|b", "a||b", "(?=a)(?!b)(?<=c)(?<!d)", "(?im-s:a.)", "(?-i)",
                     "(?P<word>a)(?P=word)", "[[:alpha:]]", "[-a-z-]",
                     "\\p{Script=Greek}\\u{1F600}\\u0041\\x20\\cA", "a{,3}a{2,}a{4}a{literal}",
                     "[\\]\\-]", "世界", "two words\n"})
        parse(str);
}
TEST_CASE("regex malformed syntax")
{
    for (auto str : {"*a", "a**", "a+++", "(", "a)", "[abc", "\\", "(?<1bad>x)", "(?<namex)",
                     "(?Q=x)", "(?i-)", "\\p{=Greek}", "\\k<>", "[a--z"})
        fail(str);
}

TEST_CASE("regex nested patterns and whitespace")
{
    auto tree = parse("(a|bc)+ d");
    CHECK(contents<grammar::capturing_group>(tree) == std::vector<std::string>{"(a|bc)"});
    CHECK(contents<grammar::pattern_character>(tree)
          == std::vector<std::string>{"a", "b", "c", " ", "d"});
    std::string deep(200, '(');
    deep += 'a';
    deep += std::string(200, ')');
    parse(deep.c_str());
    deep.pop_back();
    fail(deep.c_str());
}

TEST_CASE("regex complete document")
{
    parse(R"sample(^(?<scheme>https?)://(?:[a-z0-9-]+\.)+[a-z]{2,}(?::[0-9]+)?(?:/[^\s]*)?$
)sample");
}
