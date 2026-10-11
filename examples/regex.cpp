// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Parse regular expressions using recursive groups, alternation, quantifiers,
// and context-sensitive character classes. Supports named groups, lookarounds,
// backreferences, flags, and Unicode property escapes. Empty groups are accepted.
// The syntax combines several dialects; patterns and semantic constraints are not
// evaluated. Spaces are literal and line breaks are ignored.
//
// Usage: build/examples/regex <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_regex; test target: lexy_test_regex.
// Reference: https://github.com/tree-sitter/tree-sitter-regex/tree/v0.25.0
// Located through Nix: tree-sitter-grammars.tree-sitter-regex.src
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

    constexpr auto group_identifier
        = dsl::identifier(dsl::ascii::alpha_underscore, (dsl::ascii::alnum / dsl::lit_c<'_'>));
    struct group_name
    {
        static constexpr auto rule = group_identifier;
    };
    struct unicode_property
    {
        static constexpr auto rule
            = dsl::token(dsl::while_one((dsl::ascii::alnum / dsl::lit_c<'_'>)));
    };
    struct unicode_escape
    {
        static constexpr auto rule
            = LEXY_LIT("\\u") >> ((dsl::lit_c<'{'> >> dsl::digits<dsl::hex> + dsl::lit_c<'}'>)
                                  | dsl::else_ >> dsl::times<4>(dsl::digit<dsl::hex>));
    };
    struct property_escape
    {
        static constexpr auto rule
            = dsl::backslash + LEXY_ASCII_ONE_OF("pP") + dsl::lit_c<'{'> + dsl::p<unicode_property>
              + dsl::if_(dsl::lit_c<'='> >> dsl::p<unicode_property>) + dsl::lit_c<'}'>;
    };
    struct backreference
    {
        static constexpr auto rule = LEXY_LIT("\\k<") >> dsl::p<group_name> + dsl::lit_c<'>'>;
    };
    struct escape : lexy::token_production
    {
        static constexpr auto rule = [] {
            auto hex     = LEXY_LIT("\\x") + dsl::times<2>(dsl::digit<dsl::hex>);
            auto control = LEXY_LIT("\\c") + dsl::ascii::alpha;
            return (dsl::peek(dsl::p<unicode_escape>) >> dsl::p<unicode_escape>)
                   | (dsl::peek(dsl::backslash + LEXY_ASCII_ONE_OF("pP"))
                      >> dsl::p<property_escape>)
                   | dsl::p<backreference> | (dsl::peek(hex) >> hex)
                   | (dsl::peek(control) >> control)
                   | (dsl::backslash
                      >> (dsl::digits<> | (dsl::code_point - LEXY_ASCII_ONE_OF("kpP\n"))));
        }();
    };
    struct posix_character_class
    {
        static constexpr auto rule
            = LEXY_LIT("[:") >> dsl::while_one(dsl::ascii::alpha) + LEXY_LIT(":]");
    };
    struct class_character
    {
        static constexpr auto rule = dsl::code_point - LEXY_ASCII_ONE_OF("\\]-");
    };
    constexpr auto class_atom = dsl::p<escape> | dsl::p<class_character>;
    struct class_range
    {
        static constexpr auto rule
            = (class_atom | dsl::lit_c<'-'>)+dsl::lit_c<'-'> + (class_atom | dsl::lit_c<'-'>);
    };
    struct character_class
    {
        static constexpr auto rule
            = dsl::lit_c<'['> >> dsl::if_(dsl::lit_c<'^'>)
                                     + dsl::if_(dsl::peek(dsl::lit_c<'-'>
                                                          + dsl::peek_not(LEXY_LIT("--")))
                                                >> dsl::lit_c<'-'>)
                                     + dsl::while_((dsl::peek(dsl::p<posix_character_class>)
                                                    >> dsl::p<posix_character_class>)
                                                   | (dsl::peek(dsl::p<class_range>)
                                                      >> dsl::p<class_range>)
                                                   | class_atom)
                                     + dsl::if_(dsl::lit_c<'-'>) + dsl::lit_c<']'>;
    };
    struct pattern;
    struct named_capturing_group
    {
        static constexpr auto rule
            = (LEXY_LIT("(?<") | LEXY_LIT("(?P<")) >> dsl::p<group_name> + dsl::lit_c<'>'>
                                                          + dsl::recurse<pattern> + dsl::lit_c<')'>;
    };
    struct non_capturing_group
    {
        static constexpr auto rule = LEXY_LIT("(?:") >> dsl::recurse<pattern> + dsl::lit_c<')'>;
    };
    struct lookaround_assertion
    {
        static constexpr auto rule = (dsl::literal_set(LEXY_LIT("(?="), LEXY_LIT("(?!"))
                                      / LEXY_LIT("(?<=") / LEXY_LIT("(?<!"))
                                     >> dsl::recurse<pattern> + dsl::lit_c<')'>;
    };
    struct named_group_backreference
    {
        static constexpr auto rule = LEXY_LIT("(?P=") >> dsl::p<group_name> + dsl::lit_c<')'>;
    };
    struct inline_flags_group
    {
        static constexpr auto rule
            = LEXY_LIT("(?")
              >> ((dsl::lit_c<'-'> >> dsl::while_one(dsl::ascii::alpha))
                  | dsl::else_
                        >> dsl::while_one(dsl::ascii::alpha)
                               + dsl::if_(dsl::lit_c<'-'> >> dsl::while_one(dsl::ascii::alpha)))
                     + dsl::if_(dsl::colon >> dsl::recurse<pattern>) + dsl::lit_c<')'>;
    };
    struct capturing_group
    {
        static constexpr auto rule = dsl::lit_c<'('> >> dsl::recurse<pattern> + dsl::lit_c<')'>;
    };
    struct pattern_character
    {
        static constexpr auto rule = dsl::code_point - LEXY_ASCII_ONE_OF("^$\\.*+?()[]|\r\n");
    };
    struct assertion
    {
        static constexpr auto rule = LEXY_ASCII_ONE_OF("^$");
    };
    struct any_character
    {
        static constexpr auto rule = dsl::period;
    };
    struct count_quantifier
    {
        static constexpr auto rule
            = dsl::lit_c<'{'> >> ((dsl::comma >> dsl::digits<>)
                                  | dsl::else_
                                        >> dsl::digits<>
                                               + dsl::if_(dsl::comma >> dsl::if_(dsl::digits<>)))
                                     + dsl::lit_c<'}'>;
    };
    struct quantifier
    {
        static constexpr auto rule
            = (LEXY_ASCII_ONE_OF("*+?")
               | (dsl::peek(dsl::p<count_quantifier>) >> dsl::p<count_quantifier>))
              >> dsl::if_(dsl::lit_c<'?'>);
    };
    struct atom
    {
        static constexpr auto rule
            = dsl::p<lookaround_assertion> | dsl::p<named_group_backreference>
              | dsl::p<named_capturing_group> | dsl::p<non_capturing_group>
              | dsl::p<inline_flags_group> | dsl::p<capturing_group> | dsl::p<posix_character_class>
              | dsl::p<character_class> | dsl::p<escape> | dsl::p<assertion> | dsl::p<any_character>
              | dsl::p<pattern_character>;
    };
    struct term
    {
        static constexpr auto rule = dsl::peek(dsl::code_point - LEXY_ASCII_ONE_OF("|)"))
                                     >> dsl::list(dsl::p<atom> >> dsl::if_(dsl::p<quantifier>));
    };
    struct pattern
    {
        // Empty alternatives are meaningful, including the entirely empty expression.
        static constexpr auto rule
            = dsl::if_(dsl::p<term>) + dsl::while_(dsl::lit_c<'|'> >> dsl::if_(dsl::p<term>));
    };
    struct document
    {
        // The reference grammar ignores line breaks, but spaces remain pattern characters.
        static constexpr auto whitespace = dsl::newline;
        static constexpr auto rule       = dsl::p<pattern> + dsl::eof;
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
