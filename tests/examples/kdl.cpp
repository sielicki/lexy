// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/kdl.cpp" // NOLINT

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

TEST_CASE("kdl typed fields and child nodes")
{
    auto tree
        = parse("(app)server host=\"localhost\" port=(u16)8080 {\n paths r#\"C:\\data\"#;\n}\n");
    CHECK(contents<grammar::node>(tree).size() == 2);
    CHECK(contents<grammar::node_field>(tree).size() == 3);
    CHECK(contents<grammar::type>(tree).size() == 2);
    CHECK(contents<grammar::raw_string>(tree).size() == 1);
}
TEST_CASE("kdl comments, continuations, raw strings, and numbers")
{
    for (auto str : {"", "// eof", "/* outer /* inner */ done */", "node", "node;",
                     "node /-1 2 /-{ hidden; };", "node \\\n 1;", "node r##\"a\"#b\"##;",
                     "node 0xFF 0o77 0b10 -12 +1.5e-2;", "node \"世界\";", "节点 1\n",
                     "node \"\\u{1F600}\\n\";", "a\rb\u0085c\u2028d\u2029"})
        parse(str);
}
TEST_CASE("kdl malformed input")
{
    for (auto str : {"node { child }", "node {", "node \"unclosed", "node r##\"mismatch\"#",
                     "node 0x;", "node 1e;", "node key=;", "node bare;", "/* unclosed",
                     "node(typed)1;", "node \"\\q\";", "node /-;", "node 1.2.3;"})
        fail(str);
}

TEST_CASE("kdl strict raw delimiters and identifier boundaries")
{
    parse("node r###\"inside \"## still inside\"###;");
    parse("+ name=\"positive\"; - name=\"negative\";");
    fail("node r##\"missing final hash\"#;");
    parse("node +key=1 -key=2;");
    fail("+1 value=2;");
    fail("-2 value=3;");
}

TEST_CASE("kdl complete document")
{
    parse(R"sample(// An application configuration
app "lexy" version=1 {
  (service)server host="localhost" port=(u16)8080
  paths r#"C:\data\files"#
  /- obsolete true
  limits 0xFF 1_000 1.5e2
}
)sample");
}
