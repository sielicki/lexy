// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/query.cpp" // NOLINT

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

TEST_CASE("query captures, predicates, and fields")
{
    auto tree = parse("(call function: (identifier) @fn !type (#eq? @fn \"main\")) @call");
    CHECK(contents<grammar::named_node>(tree).size() == 2);
    CHECK(contents<grammar::field_definition>(tree).size() == 1);
    CHECK(contents<grammar::predicate>(tree).size() == 1);
    CHECK(contents<grammar::capture>(tree).size() == 3);
}
TEST_CASE("query alternatives, anchors, and special nodes")
{
    for (auto str :
         {"", "; comment", "[(a) (b)]* @items", "((a) . (b) .)", "(parent . (a) . (b) .)",
          "(MISSING)", "(MISSING \";\") @missing", "(expression/binary_expression)",
          "(expression/\"+\")", "_ @any", "(a !field)", "\"世界\"",
          "(#set! injection.language \"c\")", "(a) @a.b-c_2", "(a) ; trailing\n"})
        parse(str);
}
TEST_CASE("query malformed patterns")
{
    for (auto str : {"()", "[]", "(a", "[(_) ", "@orphan", "(a) @ name", "(a / b)", "(a/ b)",
                     "(#eq @a)", "(#eq? @)", "(a .)", "(a .. (b))", "(a field:)", "\"unterminated",
                     "\"new\nline\"", "identifier"})
        fail(str);
}

TEST_CASE("query nested anchors and adjacent tokens")
{
    parse("((parent . (child)? @child .) @parent (#eq? @child \"a;b\"))");
    fail("(expr/ \"+\")");
    fail("(# eq? @x \"a\")");
    fail("(parent !)");
    std::string deep(200, '(');
    deep += "(a)";
    deep += std::string(200, ')');
    parse(deep.c_str());
    deep.pop_back();
    fail(deep.c_str());
}

TEST_CASE("query complete document")
{
    parse(R"sample(; Match calls to uppercase names.
(call_expression
  function: (identifier) @function
  arguments: (arguments (_) @argument)
  (#match? @function "^[A-Z]")) @call

[(string) (number)] @literal
)sample");
}
