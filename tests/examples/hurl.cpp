// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/hurl.cpp" // NOLINT

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

TEST_CASE("hurl request response and captures")
{
    auto tree = parse(
        "GET https://example.org\nAccept: application/json\nHTTP 200\n[Captures]\nid: jsonpath \"$.id\"\n[Asserts]\njsonpath \"$.items\" count == 2\n");
    CHECK(contents<grammar::entry>(tree).size() == 1);
    CHECK(contents<grammar::response>(tree).size() == 1);
    CHECK(contents<grammar::capture>(tree).size() == 1);
    CHECK(contents<grammar::assertion>(tree).size() == 1);
}
TEST_CASE("hurl embedded bodies and templates")
{
    for (auto str :
         {"", "# comment", "GET https://example.org\n", "GET\nHTTP/* *\n",
          "POST /\n{\"items\": [1, {{id}}], \"empty\": []}\n",
          "POST /\n```json\n{\"raw\": {{value}}}\n```\n", "POST /\n`hello {{name}}`\n",
          "POST /\nhex,00ef;\n", "POST /\nbase64,YQ==;\n", "POST /\nfile,{{name}}.bin;\n",
          "POST /\n<?xml version=\"1.0\"?><root><child>text</child></root>\n",
          "GET /\n[Options]\nretry: true\ndelay: 100ms\nvariable: id=123\n"})
        parse(str);
}
TEST_CASE("hurl filters and sections")
{
    parse(
        "GET /\n[Query]\nkey: value\n[Cookies]\nsession: {{id}}\nHTTP 200\n[Asserts]\nheader \"X\" split \",\" nth 0 matches /a.*/\nbody not isEmpty\nbytes startsWith hex,efbbbf;\n");
    parse("POST /\n[BasicAuth]\nuser: password\n[Multipart]\nfile: file,a.txt; text/plain\n");
}
TEST_CASE("hurl malformed input")
{
    for (auto str : {"get /\n", "HTTP 200\n", "GET /\nHTTP nope\n", "GET /\n[Unknown]\n",
                     "GET /\n[Asserts]\nstatus == 200\n", "GET /\nHTTP 200\n[Asserts]\nstatus ==\n",
                     "POST /\n{\"a\":}\n", "POST /\n[1,]\n", "POST /\n```\nunclosed\n",
                     "GET /{{name\n", "GET /\n[Options]\ninsecure: maybe\n", "POST /\n<a></b>\n"})
        fail(str);
}

TEST_CASE("hurl embedded template keys and XML boundaries")
{
    parse("POST /\n{{body}}\nHTTP 200\n");
    parse(
        "GET /\n{{key}}: {{value}}\nHTTP 200\n[Asserts]\nstatus == 200 # inline comment\nbody isEmpty\n");
    parse("POST /\n<root><child key='value'/></root>\n");
    fail("Get /\n");
    fail("GEThttps://example.org\n");
    fail("POST /\n<root><child/></different>\n");
}

TEST_CASE("hurl JSON body followed by a response or another request")
{
    auto tree = parse(
        "POST /\n{\"items\": [1, 2, {\"x\": true}]}\nHTTP 201\n[Asserts]\nstatus == 201\nGET /\nHTTP 200\n[]\nGET /again\n");
    CHECK(contents<grammar::entry>(tree).size() == 3);
    CHECK(contents<grammar::response>(tree).size() == 2);
    CHECK(contents<grammar::json_object>(tree).size() == 2);
    CHECK(contents<grammar::json_array>(tree).size() == 2);
    fail("POST /\n{}HTTP 200\n");
}

TEST_CASE("hurl complete document")
{
    parse(R"sample(POST https://example.org/api/items
Content-Type: application/json
[Options]
variable: name=lexy
{"name": "{{name}}", "count": 2}

HTTP 201
[Captures]
id: jsonpath "$.id"
[Asserts]
jsonpath "$.name" == "lexy"
header "Location" exists

GET https://example.org/api/items/{{id}}
HTTP 200
)sample");
}
