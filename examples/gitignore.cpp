// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Parse gitignore patterns using line-sensitive lexing and wildcard lookahead.
// Supports negation, separators, escapes, globstars, ranges, and POSIX classes.
// An unmatched [ remains ordinary pattern text. A final newline is optional.
// This example builds a lossless tree; it does not match paths or apply ignore rules.
//
// Usage: build/examples/gitignore <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_gitignore; test target: lexy_test_gitignore.
// Reference:
// https://github.com/shunsambongi/tree-sitter-gitignore/tree/f4685bf11ac466dd278449bcfe5fd014e94aa504
// Located through Nix: tree-sitter-grammars.tree-sitter-gitignore.src
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

    constexpr auto line_char = dsl::code_point - dsl::ascii::newline;
    struct comment
    {
        static constexpr auto rule = dsl::lit_c<'#'> >> dsl::until(dsl::newline).or_eof();
    };
    struct negation
    {
        static constexpr auto rule = dsl::lit_c<'!'>;
    };
    struct directory_separator
    {
        static constexpr auto rule = dsl::lit_c<'/'>;
    };
    struct directory_separator_escaped
    {
        static constexpr auto rule = LEXY_LIT("\\/");
    };
    struct wildcard_char_single
    {
        static constexpr auto rule = dsl::lit_c<'?'>;
    };
    struct wildcard_chars
    {
        static constexpr auto rule = dsl::lit_c<'*'>;
    };
    struct wildcard_chars_allow_slash
    {
        static constexpr auto rule = LEXY_LIT("**");
    };
    struct pattern_char
    {
        static constexpr auto rule = line_char - LEXY_ASCII_ONE_OF("/*?");
    };
    struct pattern_char_escaped
    {
        static constexpr auto rule = dsl::backslash >> (line_char - dsl::lit_c<'/'>);
    };
    struct bracket_char
    {
        static constexpr auto rule = line_char - LEXY_ASCII_ONE_OF("/]");
    };
    struct bracket_char_escaped
    {
        static constexpr auto rule = dsl::backslash >> (line_char - dsl::lit_c<'/'>);
    };
    constexpr auto bracket_atom = dsl::p<bracket_char_escaped> | dsl::p<bracket_char>;
    struct bracket_range
    {
        static constexpr auto rule = bracket_atom + dsl::lit_c<'-'> + bracket_atom;
    };
    struct bracket_char_class
    {
        static constexpr auto rule
            = LEXY_LIT("[:")
              >> (dsl::literal_set(LEXY_LIT("alnum"), LEXY_LIT("alpha")) / LEXY_LIT("blank")
                  / LEXY_LIT("cntrl") / LEXY_LIT("digit") / LEXY_LIT("graph") / LEXY_LIT("lower")
                  / LEXY_LIT("print") / LEXY_LIT("punct") / LEXY_LIT("space") / LEXY_LIT("upper")
                  / LEXY_LIT("xdigit"))
                     + LEXY_LIT(":]");
    };
    struct bracket_negation
    {
        static constexpr auto rule = LEXY_ASCII_ONE_OF("!^");
    };
    struct bracket_expr
    {
        static constexpr auto rule = [] {
            auto element
                = (dsl::peek(dsl::p<bracket_char_class>) >> dsl::p<bracket_char_class>)
                  | (dsl::peek(dsl::p<bracket_range>) >> dsl::p<bracket_range>) | bracket_atom;
            auto first = (dsl::lit_c<']'> >> dsl::if_(dsl::lit_c<'-'> >> bracket_atom)) | element;
            return dsl::lit_c<'['> >> dsl::if_(dsl::p<bracket_negation>) + first
                                          + dsl::while_(element) + dsl::lit_c<']'>;
        }();
    };
    struct pattern
    {
        static constexpr auto rule = [] {
            auto separator = dsl::p<directory_separator_escaped> | dsl::p<directory_separator>;
            // Like the highlighting grammar, an unmatched '[' is an ordinary character.
            auto element = dsl::p<wildcard_chars_allow_slash> | dsl::p<wildcard_chars>
                           | dsl::p<wildcard_char_single>
                           | (dsl::peek(dsl::p<bracket_expr>) >> dsl::p<bracket_expr>)
                           | (dsl::peek(dsl::backslash + (line_char - dsl::lit_c<'/'>))
                              >> dsl::p<pattern_char_escaped>)
                           | dsl::p<pattern_char>;
            auto segment = dsl::while_one(element);
            return dsl::if_(dsl::p<negation>) + dsl::if_(separator) + segment
                   + dsl::while_(separator >> (dsl::peek(dsl::eol) | dsl::else_ >> segment));
        }();
    };
    struct document
    {
        // A final unterminated line is accepted, as it is by Git itself.
        static constexpr auto rule
            = dsl::while_(dsl::newline | dsl::p<comment>
                          | (dsl::peek_not(dsl::eof) >> dsl::p<pattern> + dsl::eol))
              + dsl::eof;
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
