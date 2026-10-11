// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Parse Ungrammar using recursive productions and lookahead across whitespace.
// Recognizing identifier = ends the preceding definition, even without a newline.
// Labels, sequences, alternatives, and postfix operators form a lossless parse tree.
// References are not resolved and no parser is generated.
//
// Usage: build/examples/ungrammar <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_ungrammar; test target: lexy_test_ungrammar.
// Reference:
// https://github.com/Philipp-M/tree-sitter-ungrammar/tree/debd26fed283d80456ebafa33a06957b0c52e451
// Located through Nix: tree-sitter-grammars.tree-sitter-ungrammar.src
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

    constexpr auto comment    = LEXY_LIT("//") >> dsl::until(dsl::newline).or_eof();
    constexpr auto trivia     = dsl::ascii::space | comment;
    constexpr auto identifier = dsl::token(dsl::while_one(dsl::ascii::alpha / dsl::lit_c<'_'>));
    // Newlines do not terminate rules. Look ahead for the next definition instead.
    constexpr auto definition_start = identifier + dsl::while_(trivia) + dsl::lit_c<'='>;

    struct definition
    {
        static constexpr auto rule = identifier;
    };
    struct node_rule
    {
        static constexpr auto rule = identifier;
    };
    struct token : lexy::token_production
    {
        static constexpr auto rule
            = dsl::lit_c<'\''> >> dsl::while_one((dsl::code_point - LEXY_ASCII_ONE_OF("'\\"))
                                                 | (dsl::backslash >> LEXY_ASCII_ONE_OF("'\\")))
                                      + dsl::lit_c<'\''>;
    };
    struct alternation;
    struct atom;
    struct label
    {
        static constexpr auto rule = identifier + dsl::colon + dsl::recurse<atom>;
    };
    struct atom
    {
        static constexpr auto rule
            = ((dsl::lit_c<'('> >> dsl::recurse<alternation> + dsl::lit_c<')'>)
               | (dsl::peek(identifier + dsl::while_(trivia) + dsl::colon) >> dsl::p<label>)
               | dsl::p<node_rule> | dsl::p<token>);
    };
    struct repetition
    {
        static constexpr auto rule = dsl::lit_c<'*'>;
    };
    struct optional
    {
        static constexpr auto rule = dsl::lit_c<'?'>;
    };
    struct term
    {
        static constexpr auto rule
            = dsl::peek(dsl::peek_not(definition_start)
                        + (identifier | dsl::lit_c<'('> | dsl::lit_c<'\''>))
              >> dsl::p<atom> + dsl::while_(dsl::p<repetition> | dsl::p<optional>);
    };
    struct sequence
    {
        static constexpr auto rule = dsl::list(dsl::p<term>);
    };
    struct alternation
    {
        static constexpr auto rule = dsl::list(dsl::p<sequence>, dsl::sep(dsl::lit_c<'|'>));
    };
    struct node
    {
        static constexpr auto rule = dsl::p<definition> >> dsl::lit_c<'='> + dsl::p<alternation>;
    };
    struct document
    {
        static constexpr auto whitespace = trivia;
        static constexpr auto rule       = dsl::while_(dsl::p<node>) + dsl::eof;
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
