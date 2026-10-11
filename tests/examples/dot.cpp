// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/dot.cpp" // NOLINT

#include <doctest/doctest.h>
#include <lexy/action/match.hpp>
#include <lexy/callback.hpp>
#include <lexy/input/string_input.hpp>
#include <string>

namespace
{
void pass(const char* str)
{
    INFO(str);
    REQUIRE(lexy::match<grammar::graph>(lexy::zstring_input<lexy::utf8_encoding>(str)));
}

void fail(const char* str)
{
    INFO(str);
    REQUIRE(!lexy::match<grammar::graph>(lexy::zstring_input<lexy::utf8_encoding>(str)));
}
} // namespace

TEST_CASE("DOT graph and keywords")
{
    pass("graph {}");
    pass("digraph pipeline {}");
    pass("StRiCt DiGrApH G { NoDe []; EdGe []; GrApH [] }");
    pass("strict graph G { subgraph {} }");
    pass("graph { graph_name; node2; edge_case; strictness; subgraph_1; digraph2 }");
    pass(R"(graph "graph" { "node" -- "edge" })");

    fail("");
    fail("{}");
    fail("strict {}");
    fail("graphical {}");
    fail("strictgraph {}");
    fail("graph strict {}");
    fail("graph { DiGraph }");
    fail("graph { node }");
    fail("graph { subgraph }");
    fail("graph {} graph {}");
    fail("graph {} trailing");
    fail("graph {");
    fail("graph { a;");
    fail("graph { ; }");
    fail("graph { a;; }");
}

TEST_CASE("DOT identifiers and ports")
{
    pass("graph { _; node_2; café; 节点; a\xCC\x81 }");
    pass("graph { 0; 007; -12; 3.; -4.5; .25; -.5 }");
    pass("graph 1 { a:n; b:output:sw; c:arbitrary; d:0:<port> }");
    pass(R"(graph { ""; "hello world"; "a" + /* join */ "b" + "c" })");
    pass(R"(graph { "quote: \" backslash: \\ label: \N\l" })");
    pass("graph { \"two\nlines\" }");
    pass("graph { \"continued\\\nline\" }");
    pass("graph { \"continued\\\r\nline\" }");

    fail("graph { +1 }");
    fail("graph { -. }");
    fail("graph { . }");
    fail("graph { - }");
    fail("graph { a: }");
    fail("graph { a:p: }");
    fail("graph { a:p:n:e }");
    fail("graph { a::n }");
    fail("graph { 'single quoted' }");
    fail("graph { \"unterminated }");
    fail("graph { \"trailing escape\\");
    fail(R"(graph { "a" + })");
    fail(R"(graph { "a" + b })");
    fail(R"(graph { a + "b" })");
    fail("graph { \xFF }");
}

TEST_CASE("DOT HTML-like identifiers")
{
    pass("graph { <>; <plain text>; <<B>bold</B>> }");
    pass("graph { <a\n<b<c>>\nd> }");
    pass(R"(graph { a [label=<<TABLE><TR><TD PORT="p">x</TD></TR></TABLE>>] })");
    // These are strings, so comment markers do not consume closing delimiters.
    pass("graph { <// literal>; <# literal>; </* literal> }");

    fail("graph { < }");
    fail("graph { <<B>text</B> }");
    fail("graph { <text>> }");
    fail("graph { <a> + <b> }");
}

TEST_CASE("DOT attributes and shared statement prefixes")
{
    pass("graph { label=diagram; a; a:p; a:p -- b; a [label=node_name] }");
    pass("graph { graph []; node [shape=box]; edge [color=blue] }");
    pass("graph { a [x=1 y=2, z=3;][label=hi,][]; b [] }");
    pass(R"(graph { "node" ["graph"="edge"]; <name>=<value> })");
    pass("graph { a=x b=y c=z }");

    fail("graph { graph }");
    fail("graph { node=box }");
    fail("graph { a= }");
    fail("graph { =b }");
    fail("graph { a:p=b }");
    fail("graph { a=b [x=y] }");
    fail("graph { a [x] }");
    fail("graph { a [x=] }");
    fail("graph { a [,x=y] }");
    fail("graph { a [x=y,,] }");
    fail("graph { a [x=y }");
    fail("graph { a [x=y] -- b }");
}

TEST_CASE("DOT edges and subgraphs")
{
    pass("digraph { a:out:e -> b:in:w -> c [label=flow] }");
    pass("graph { a -- b -- c [weight=2][] }");
    pass("graph { subgraph cluster_0 { a; { b -- c } } }");
    pass("digraph { {a b} -> {c d} -> e }");
    pass("digraph { subgraph left { a } -> subgraph right { b } [color=blue] }");
    pass("digraph { a -> subgraph { b -> c } -> {} }");
    pass("graph { {} -- {} }");
    pass("strict graph { a -- b; a -- b }");

    fail("graph { a -> b }");
    fail("digraph { a -- b }");
    fail("digraph { a -> b -- c }");
    fail("graph { subgraph { a -> b } }");
    fail("digraph { a -> { b -- c } }");
    fail("digraph { {} -- {} }");
    fail("graph { a -- }");
    fail("digraph { a -> -> b }");
    fail("digraph { -> b }");
    fail("graph { subgraph s }");
    fail("graph { subgraph s { a }");
    fail("graph { {} [label=invalid] }");
    fail("graph { a, b }");
}

TEST_CASE("DOT comments and whitespace")
{
    pass("// heading\ngraph {} // no final newline");
    pass("# 1 input.dot\ngraph {} # end");
    pass("/* heading */ graph/* kind */G{a/* node */--/* edge */b} /**/");
    pass("graph { // line\n a -- b /* multiple\n lines */ # directive\n }");
    pass("\r\n\tgraph\vG\f{a\r\n--\tb}\r\n");
    pass(R"(graph { "//not a comment" [label="/* literal */ # literal"] })");

    fail("// graph {}");
    fail("graph {/* unfinished }");
    fail("graph {} /* unfinished");
    fail("graph { a - /* split operator */ - b }");
    fail("graph { a -- b // closing brace is commented out }");
}

TEST_CASE("DOT lossless parse tree")
{
    const char*                           source = "/* header */ digraph G {\n"
                                                   "  subgraph cluster { a:out -> b [label=\"x\" + \"y\"] }\n"
                                                   "  a -> { c d } [label=<<B>html</B>>];\n"
                                                   "} // end";
    auto                                  input  = lexy::zstring_input<lexy::utf8_encoding>(source);
    lexy::parse_tree_for<decltype(input)> tree;
    REQUIRE(lexy::parse_as_tree<grammar::graph>(tree, input, lexy::noop));
    CHECK(tree.root().kind() == grammar::graph{});
    CHECK(tree.remaining_input().empty());

    std::string reconstructed;
    auto        subgraphs = 0;
    auto        edges     = 0;
    for (auto [event, node] : tree.traverse())
    {
        if (event == lexy::traverse_event::leaf)
            reconstructed.append(node.lexeme().begin(), node.lexeme().end());
        else if (event == lexy::traverse_event::enter)
        {
            if (node.kind() == grammar::subgraph{})
                ++subgraphs;
            if (node.kind() == grammar::edge_operator{})
                ++edges;
        }
    }
    CHECK(reconstructed == source);
    CHECK(subgraphs == 2);
    CHECK(edges == 2);
}

TEST_CASE("DOT recursion limit")
{
    // Both kinds of nesting use recurse, so malformed/deep input fails in a bounded way.
    auto subgraphs
        = std::string("graph {") + std::string(2'000, '{') + "a" + std::string(2'000, '}') + "}";
    fail(subgraphs.c_str());
    auto html
        = std::string("graph {") + std::string(2'000, '<') + "text" + std::string(2'000, '>') + "}";
    fail(html.c_str());

    auto label = std::string("graph { a [label=") + std::string(2'000, '<') + "text"
                 + std::string(2'000, '>') + "] }";
    fail(label.c_str());
}
