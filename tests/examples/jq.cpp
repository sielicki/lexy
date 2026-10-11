// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/jq.cpp" // NOLINT

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

TEST_CASE("jq expression precedence")
{
    auto tree = parse(".items[] | .price + 2 * 3 // 0");
    CHECK(contents<grammar::expression::pipe>(tree).size() == 1);
    CHECK(contents<grammar::expression::product>(tree).size() == 1);
    CHECK(contents<grammar::expression::sum>(tree).size() == 1);
    CHECK(contents<grammar::expression::alternative>(tree).size() == 1);
}
TEST_CASE("jq functions and control flow")
{
    for (auto str :
         {"def f($x; g): g + $x; f(1; .)", "if .a then 1 elif .b then 2 else 3 end",
          "try .foo catch null", "reduce .[] as $x (0; . + $x)", "foreach .[] as $x (0; . + $x; .)",
          "label $out | break $out", "def only: .;", "include \"lib\"; .",
          "module {}; import \"x\" as x; x::f", "[.a, .b] | {name: .[0], (\"value\"): (.[1] + 1),}",
          ".a[1:3]?, .[:2], .[2:], .[]", ".a |= .+1", ".b //= 0", "1.5e-2", "@uri \"hello\"",
          "# comment\n. | .."})
        parse(str);
}
TEST_CASE("jq interpolation and destructuring")
{
    auto tree = parse("\"世界: \\(.value + 1)\"");
    CHECK(contents<grammar::string_interpolation>(tree).size() == 1);
    parse(
        ".records[] as {$title, detail: {$score}} ?// {$title, detail: [{$score}]} | {$title, $score}");
    parse("\"Next counter: \\(.counter + 1); current: \\(.counter)\"");
    parse(". as [$x, $y] | $x + $y");
}
TEST_CASE("jq malformed queries")
{
    for (auto str : {"", ". |", "1 +", "if . then 1", "if . 1 end", "def f: .", "[1,]", "{a:}",
                     ".a[", "\"\\(1+)\"", "\"\\q\"", "reduce .[] as (0;.)", "import \"x\"; .",
                     ". as $", "foo()", "then", "1 2"})
        fail(str);
}

TEST_CASE("jq object values and constant metadata")
{
    auto tree = parse("{a: .items | length, b: 2 + 3 * 4}");
    CHECK(contents<grammar::object_entry>(tree).size() == 2);
    CHECK(contents<grammar::object_value::pipe>(tree).size() == 1);
    parse(
        "module {version: 1, names: [\"世界\"], enabled: true}; import \"lib\" as lib {search: [\".\"]}; lib::f");
    fail("module {value: .}; .");
    fail("module {value: \"\\(.)\"}; .");
    fail(". as {plain} | .");
}

TEST_CASE("jq interpolation preserves surrounding literal whitespace")
{
    auto tree = parse("\"prefix \\( . + 1 )  suffix\"");
    CHECK(contents<grammar::string_interpolation>(tree) == std::vector<std::string>{"\\( . + 1 )"});
    fail("\"\\(.)\ntext\"");
    parse("reduce -1 as $x (0; . + $x)");
}

TEST_CASE("jq complete document")
{
    parse(R"sample(# Select active users and calculate a summary.
def active: select(.active == true);
.users | map(active | {name: .name, score: (.points * 2)})
)sample");
}
