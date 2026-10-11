// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Parse DOT graphs using recursive subgraphs, balanced HTML-like identifiers, and
// a context flag that enforces directed versus undirected edge operators. Comments,
// whitespace, and identifier spellings are preserved in a lossless parse tree.
// This example parses syntax; it does not render graphs or interpret attributes.
//
// Usage: build/examples/dot <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_dot; test target: lexy_test_dot.
// Reference:
// https://github.com/rydesun/tree-sitter-dot/tree/80327abbba6f47530edeb0df9f11bd5d5c93c14d Located
// through Nix: tree-sitter-grammars.tree-sitter-dot.src Nixpkgs revision:
// 8edc0c72e3a38faf5434e40d1f19431125fe4b30.

#include <cstdio>

#include <lexy/action/parse_as_tree.hpp> // lexy::parse_as_tree
#include <lexy/dsl.hpp>                  // lexy::dsl::*
#include <lexy/input/file.hpp>           // lexy::read_file
#include <lexy/parse_tree.hpp>           // lexy::parse_tree
#include <lexy/visualize.hpp>            // lexy::visualize
#include <lexy_ext/report_error.hpp>     // lexy_ext::report_error

namespace
{
namespace grammar
{
    namespace dsl = lexy::dsl;

    // Keywords are case-insensitive; ordinary identifiers retain their case.
    constexpr auto identifier  = dsl::identifier(dsl::unicode::xid_start / dsl::lit_c<'_'>,
                                                 dsl::unicode::xid_continue / dsl::lit_c<'_'>);
    constexpr auto kw_strict   = dsl::ascii::case_folding(LEXY_KEYWORD("strict", identifier));
    constexpr auto kw_graph    = dsl::ascii::case_folding(LEXY_KEYWORD("graph", identifier));
    constexpr auto kw_digraph  = dsl::ascii::case_folding(LEXY_KEYWORD("digraph", identifier));
    constexpr auto kw_subgraph = dsl::ascii::case_folding(LEXY_KEYWORD("subgraph", identifier));
    constexpr auto kw_node     = dsl::ascii::case_folding(LEXY_KEYWORD("node", identifier));
    constexpr auto kw_edge     = dsl::ascii::case_folding(LEXY_KEYWORD("edge", identifier));

    struct number : lexy::token_production
    {
        // DOT numbers are identifiers, so retain their spelling instead of converting to float.
        // There is no exponent, leading '+', or whitespace inside a number.
        static constexpr auto rule = dsl::token(
            dsl::if_(dsl::lit_c<'-'>)
            + (dsl::period >> dsl::digits<>
               | dsl::digits<> >> dsl::if_(dsl::period >> dsl::while_(dsl::ascii::digit))));
    };

    struct string_literal : lexy::token_production
    {
        // Preserve escapes (including Graphviz's \N and \l) and line continuations in the tree.
        // Comments and whitespace inside quotes are ordinary string content.
        static constexpr auto rule
            = dsl::quoted(dsl::code_point, dsl::backslash_escape.rule(dsl::code_point));
    };

    struct quoted_id
    {
        // Only quoted strings may be concatenated. Whitespace/comments may surround '+'.
        static constexpr auto rule
            = dsl::p<string_literal> >> dsl::while_(dsl::lit_c<'+'> >> dsl::p<string_literal>);
    };

    struct html_id : lexy::token_production
    {
        // Like the Tree-sitter grammar, balance angle brackets without validating XML or
        // Graphviz's HTML label vocabulary. Token productions disable whitespace skipping.
        static constexpr auto rule = dsl::lit_c<'<'> >> dsl::loop(dsl::lit_c<'>'> >> dsl::break_
                                                                  | dsl::recurse_branch<html_id>
                                                                  | dsl::else_ >> dsl::code_point);
    };

    struct id
    {
        static constexpr auto rule
            = identifier.reserve(kw_strict, kw_graph, kw_digraph, kw_subgraph, kw_node, kw_edge)
              | dsl::p<number> | dsl::p<quoted_id> | dsl::p<html_id>;
    };

    struct port
    {
        // As in Graphviz, accept any ID for the compass point, not only n/ne/e/etc.
        static constexpr auto rule = dsl::colon >> dsl::p<id> + dsl::if_(dsl::colon >> dsl::p<id>);
    };

    struct node_id
    {
        static constexpr auto rule = dsl::p<id> >> dsl::if_(dsl::p<port>);
    };

    struct attribute
    {
        static constexpr auto rule = dsl::p<id> >> dsl::lit_c<'='> + dsl::p<id>;
    };

    struct attribute_list
    {
        // Separators are optional, including after the final attribute.
        static constexpr auto rule = dsl::square_bracketed(
            dsl::while_(dsl::p<attribute> >> dsl::if_(dsl::comma | dsl::semicolon)));
    };

    struct attributes
    {
        // Multiple attribute blocks are allowed: [color=blue][weight=2].
        static constexpr auto rule = dsl::list(dsl::p<attribute_list>);
    };

    struct block;

    struct subgraph
    {
        static constexpr auto rule = kw_subgraph >> dsl::opt(dsl::p<id>) + dsl::recurse<block>
                                     | dsl::peek(dsl::lit_c<'{'>) >> dsl::recurse<block>;
    };

    // This flag is created by the root production and inherited by all nested subgraphs.
    struct graph_kind;
    constexpr auto directed = dsl::context_flag<graph_kind>;

    struct edge_operator
    {
        struct expected_directed_edge
        {
            static constexpr auto name = "digraph requires '->' edges";
        };
        struct expected_undirected_edge
        {
            static constexpr auto name = "graph requires '--' edges";
        };

        static constexpr auto rule
            = LEXY_LIT("->")
                  >> (directed.is_set() | dsl::else_ >> dsl::error<expected_undirected_edge>)
              | LEXY_LIT("--")
                    >> (directed.is_reset() | dsl::else_ >> dsl::error<expected_directed_edge>);
    };

    struct edge_rhs
    {
        // An edge can connect nodes or whole subgraphs, and can be part of a chain.
        static constexpr auto rule
            = dsl::list(dsl::p<edge_operator> >> (dsl::p<subgraph> | dsl::p<node_id>));
    };

    struct attribute_statement
    {
        static constexpr auto rule = (kw_graph | kw_node | kw_edge) >> dsl::p<attributes>;
    };

    struct subgraph_statement
    {
        // A standalone subgraph has no trailing attributes; an edge starting with one can.
        static constexpr auto rule
            = dsl::p<subgraph> >> dsl::if_(dsl::p<edge_rhs> >> dsl::if_(dsl::p<attributes>));
    };

    struct id_statement
    {
        static constexpr auto rule = [] {
            // Assignments, node declarations, and edges share an ID prefix. Parse it once,
            // then choose using '='; no speculative parsing of the entire statement is needed.
            auto assignment   = dsl::lit_c<'='> >> dsl::p<id>;
            auto node_or_edge = dsl::else_ >> dsl::if_(dsl::p<port>) + dsl::if_(dsl::p<edge_rhs>)
                                                  + dsl::if_(dsl::p<attributes>);
            return dsl::p<id> >> (assignment | node_or_edge);
        }();
    };

    struct statement : lexy::transparent_production
    {
        static constexpr auto rule
            = dsl::p<attribute_statement> | dsl::p<subgraph_statement> | dsl::p<id_statement>;
    };

    struct block
    {
        // Stop on the first failed statement, including a recursion-limit error. Using an
        // explicit loop avoids attempting list recovery at the same opening delimiter.
        static constexpr auto rule
            = dsl::curly_bracketed(dsl::while_(dsl::p<statement> >> dsl::if_(dsl::semicolon)));
    };

    struct graph
    {
        static constexpr auto whitespace
            = dsl::ascii::space
              | (LEXY_LIT("//") | dsl::lit_c<'#'>) >> dsl::until(dsl::newline).or_eof()
              | LEXY_LIT("/*") >> dsl::until(LEXY_LIT("*/"));

        static constexpr auto rule = directed.create() + dsl::if_(kw_strict)
                                     + (kw_digraph >> directed.set() | kw_graph)
                                     + dsl::opt(dsl::p<id>) + dsl::p<block> + dsl::eof;
    };
} // namespace grammar
} // namespace

using production = grammar::graph;

#if !defined(LEXY_TEST) && !defined(LEXY_PLAYGROUND_PRODUCTION)
int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <filename>\n", argv[0]);
        return 1;
    }

    auto file = lexy::read_file<lexy::utf8_encoding>(argv[1]);
    if (!file)
    {
        std::fprintf(stderr, "unable to read '%s'\n", argv[1]);
        return 1;
    }

    // A lossless tree keeps the original spelling, comments, and whitespace. No value
    // callbacks or application-specific AST are necessary for this parsing action.
    lexy::parse_tree<lexy::input_reader<decltype(file.buffer())>> tree;
    auto result = lexy::parse_as_tree<grammar::graph>(tree, file.buffer(),
                                                      lexy_ext::report_error.path(argv[1]));
    if (!result)
        return 2;

    lexy::visualize(stdout, tree);
}
#endif
