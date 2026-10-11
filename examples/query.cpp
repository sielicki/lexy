// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Parse Tree-sitter query syntax using recursive productions, lookahead, and
// adjacent tokens. Supports fields, captures, predicates, anchors, alternatives,
// quantifiers, supertypes, and MISSING nodes. The lossless tree retains comments.
// Node names and predicates are parsed without resolving or executing them.
//
// Usage: build/examples/query <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_query; test target: lexy_test_query.
// Reference: https://github.com/tree-sitter-grammars/tree-sitter-query/tree/v0.8.0
// Located through Nix: tree-sitter-grammars.tree-sitter-query.src
// Nixpkgs revision: 8edc0c72e3a38faf5434e40d1f19431125fe4b30.

#include <cstdio>
#include <lexy/action/parse_as_tree.hpp>
#include <lexy/dsl.hpp>
#include <lexy/input/file.hpp>
#include <lexy/parse_tree.hpp>
#include <lexy/visualize.hpp>
#include <lexy_ext/report_error.hpp>

namespace
{
namespace grammar
{
    namespace dsl = lexy::dsl;

    constexpr auto comment  = dsl::lit_c<';'> >> dsl::until(dsl::newline).or_eof();
    constexpr auto trivia   = dsl::ascii::space | comment;
    constexpr auto id_start = dsl::ascii::alnum / LEXY_ASCII_ONE_OF("-_");
    constexpr auto id_rest  = id_start / dsl::lit_c<'.'>;
    constexpr auto id       = dsl::identifier(id_start, id_rest);
    struct identifier
    {
        static constexpr auto rule = id;
    };
    struct string : lexy::token_production
    {
        static constexpr auto rule
            = dsl::lit_c<'"'> >> dsl::while_((dsl::code_point - LEXY_ASCII_ONE_OF("\"\\\n"))
                                             | (dsl::backslash
                                                >> (dsl::code_point - dsl::lit_c<'\n'>)))
                                     + dsl::lit_c<'"'>;
    };
    struct capture : lexy::token_production
    {
        static constexpr auto rule = dsl::lit_c<'@'> >> id;
    };
    struct quantifier
    {
        static constexpr auto rule = LEXY_ASCII_ONE_OF("*+?");
    };
    constexpr auto suffix = dsl::while_(dsl::p<capture> | dsl::p<quantifier>);
    struct definition;
    struct negated_field
    {
        static constexpr auto rule = dsl::lit_c<'!'> >> id;
    };
    struct field_definition
    {
        static constexpr auto rule = dsl::p<identifier> + dsl::colon + dsl::recurse<definition>;
    };
    struct predicate_name : lexy::token_production
    {
        static constexpr auto rule = LEXY_ASCII_ONE_OF("#.") >> id + LEXY_ASCII_ONE_OF("?!");
    };
    struct predicate
    {
        static constexpr auto rule
            = dsl::lit_c<'('> + dsl::p<predicate_name>
              + dsl::while_(dsl::p<capture> | dsl::p<string> | dsl::p<identifier>)
              + dsl::lit_c<')'>;
    };
    struct missing_node
    {
        static constexpr auto rule = dsl::lit_c<'('> + LEXY_KEYWORD("MISSING", id)
                                     + dsl::if_(dsl::p<string> | dsl::p<identifier>)
                                     + dsl::lit_c<')'> + suffix;
    };
    struct node_name : lexy::token_production
    {
        // A supertype/name pair has no whitespace around '/'.
        static constexpr auto rule = id + dsl::if_(dsl::lit_c<'/'> >> (dsl::p<string> | id));
    };
    struct named_node
    {
        static constexpr auto rule
            = dsl::lit_c<'('> + dsl::p<node_name>
              + dsl::if_(
                  dsl::peek_not(dsl::lit_c<')'>)
                  >> dsl::if_(dsl::period)
                         + (dsl::p<negated_field> | dsl::else_ >> dsl::recurse<definition>)+dsl::
                             while_(
                                 dsl::peek_not(dsl::lit_c<')'>)
                                 >> dsl::if_(dsl::period)
                                        + dsl::if_(dsl::peek_not(dsl::lit_c<')'>)
                                                   >> (dsl::p<negated_field>
                                                       | dsl::else_ >> dsl::recurse<definition>))))
              + dsl::lit_c<')'> + suffix;
    };
    struct grouping
    {
        static constexpr auto rule
            = dsl::lit_c<'('> + dsl::recurse<definition> + dsl::if_(dsl::period)
              + dsl::while_(dsl::peek_not(dsl::lit_c<')'>) >> dsl::recurse<definition>
                                                                  + dsl::if_(dsl::period))
              + dsl::lit_c<')'> + suffix;
    };
    struct list
    {
        static constexpr auto rule
            = dsl::lit_c<'['> >> dsl::recurse<definition>
                                     + dsl::while_(dsl::peek_not(dsl::lit_c<']'>)
                                                   >> dsl::recurse<definition>)
                                     + dsl::lit_c<']'> + suffix;
    };
    struct anonymous_node
    {
        static constexpr auto rule = (dsl::p<string> | LEXY_KEYWORD("_", id)) >> suffix;
    };
    struct definition
    {
        static constexpr auto rule = [] {
            auto open = dsl::lit_c<'('> + dsl::while_(trivia);
            return (dsl::peek(open + LEXY_ASCII_ONE_OF("#.")) >> dsl::p<predicate>)
                   | (dsl::peek(open + LEXY_KEYWORD("MISSING", id)) >> dsl::p<missing_node>)
                   | (dsl::peek(open + id_start) >> dsl::p<named_node>)
                   | (dsl::peek(dsl::lit_c<'('>) >> dsl::p<grouping>) | dsl::p<list>
                   | (dsl::peek(id + dsl::while_(trivia) + dsl::colon) >> dsl::p<field_definition>)
                   | dsl::p<anonymous_node>;
        }();
    };
    struct document
    {
        static constexpr auto whitespace = trivia;
        static constexpr auto rule       = dsl::while_(dsl::p<definition>) + dsl::eof;
    };
} // namespace grammar
} // namespace

using production = grammar::document;

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
    lexy::parse_tree_for<decltype(file.buffer())> tree;
    if (!lexy::parse_as_tree<grammar::document>(tree, file.buffer(),
                                                lexy_ext::report_error.path(argv[1])))
        return 2;
    lexy::visualize(stdout, tree);
}
#endif
