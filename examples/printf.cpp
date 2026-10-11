// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Parse printf-style format strings using longest matching and lookahead to
// distinguish flags from length modifiers. The input is plain text, without C quotes.
// Multiple flags and any ASCII conversion letter are accepted; positional arguments
// are unsupported. This example parses syntax without formatting values.
//
// Usage: build/examples/printf <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_printf; test target: lexy_test_printf.
// Reference:
// https://github.com/tree-sitter-grammars/tree-sitter-printf/tree/ec4e5674573d5554fccb87a887c97d4aec489da7
// Located through Nix: vimPlugins.nvim-treesitter.builtGrammars.printf.src
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

    struct size
    {
        // A literal set uses longest matching, so hh/ll/I32/I64 win over h/l/I.
        static constexpr auto rule
            = dsl::literal_set(LEXY_LIT("hh"), LEXY_LIT("h")) / LEXY_LIT("ll") / LEXY_LIT("l")
              / LEXY_LIT("L") / LEXY_LIT("j") / LEXY_LIT("z") / LEXY_LIT("t") / LEXY_LIT("I32")
              / LEXY_LIT("I64") / LEXY_LIT("I") / LEXY_LIT("w");
    };
    struct flags
    {
        static constexpr auto rule = [] {
            auto flag = LEXY_ASCII_ONE_OF("-#0 +'I");
            // I is both a locale flag and a length modifier. Preserve I/I32/I64 as a
            // modifier when followed by a conversion letter; otherwise it can be a flag.
            auto condition = dsl::peek_not(dsl::p<size> + dsl::ascii::alpha) + flag;
            return dsl::token(dsl::while_one(dsl::peek(condition) >> flag));
        }();
    };
    struct width
    {
        static constexpr auto rule = dsl::digits<> | dsl::lit_c<'*'>;
    };
    struct precision
    {
        // A bare dot specifies zero precision. '*' takes the precision from an argument.
        static constexpr auto rule
            = dsl::period >> (dsl::lit_c<'*'> | dsl::else_ >> dsl::while_(dsl::ascii::digit));
    };
    struct type
    {
        // Follow the source grammar's broad alphabet, including extensions such as Go's v/q.
        static constexpr auto rule = dsl::ascii::alpha;
    };
    struct format
    {
        static constexpr auto rule = dsl::lit_c<'%'> >> dsl::if_(dsl::p<flags>)
                                                            + dsl::if_(dsl::p<width>)
                                                            + dsl::if_(dsl::p<precision>)
                                                            + dsl::if_(dsl::p<size>) + dsl::p<type>;
    };
    struct text : lexy::transparent_production
    {
        // Whitespace is text, including newlines. No automatic whitespace skipping is used.
        static constexpr auto rule = dsl::token(dsl::while_one(dsl::code_point - dsl::lit_c<'%'>));
    };
    struct format_string
    {
        // Handle %% before committing to a conversion beginning with the same prefix.
        static constexpr auto rule
            = dsl::while_(LEXY_LIT("%%") | dsl::p<format> | dsl::p<text>) + dsl::eof;
    };
} // namespace grammar
} // namespace

using production = grammar::format_string;

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
    if (!lexy::parse_as_tree<grammar::format_string>(tree, file.buffer(),
                                                     lexy_ext::report_error.path(argv[1])))
        return 2;
    lexy::visualize(stdout, tree);
}
#endif
