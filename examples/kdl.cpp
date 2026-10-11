// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Parse KDL v1 using explicit node whitespace and recursive comments/children.
// Supports typed values, properties, slash-dash comments, and line continuations.
// A scanner matches raw-string hash delimiters; quoted strings validate escapes.
// Children need a terminator before }. This example builds a lossless syntax tree.
//
// Usage: build/examples/kdl <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_kdl; test target: lexy_test_kdl.
// Reference: https://github.com/tree-sitter-grammars/tree-sitter-kdl/tree/v1.1.0
// Located through Nix: tree-sitter-grammars.tree-sitter-kdl.src
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

    constexpr auto newline = dsl::newline | dsl::lit_c<'\r'> | dsl::lit_cp<0x85> | dsl::lit_c<'\f'>
                             | dsl::lit_cp<0x2028> | dsl::lit_cp<0x2029>;
    struct multi_line_comment
    {
        static constexpr auto rule
            = LEXY_LIT("/*")
              >> dsl::while_(dsl::peek_not(LEXY_LIT("*/"))
                             >> ((dsl::peek(LEXY_LIT("/*")) >> dsl::recurse<multi_line_comment>)
                                 | dsl::code_point))
                     + LEXY_LIT("*/");
    };
    constexpr auto ws = (dsl::unicode::space - dsl::unicode::newline)
                        | dsl::lit_cp<0xFEFF> | dsl::p<multi_line_comment>;
    struct single_line_comment
    {
        static constexpr auto rule
            = LEXY_LIT("//")
              >> dsl::while_(dsl::code_point - dsl::unicode::newline) + (newline | dsl::eof);
    };
    constexpr auto node_space
        = ws | (dsl::backslash >> dsl::while_(ws) + (dsl::p<single_line_comment> | newline));
    constexpr auto line_space = ws | newline | dsl::p<single_line_comment>;
    struct raw_string : lexy::token_production
    {
        using scan_result          = lexy::scan_result<void>;
        static constexpr auto rule = dsl::lit_c<'r'> >> dsl::scan;
        template <typename Context, typename Reader>
        static lexy::scan_result<void> scan(lexy::rule_scanner<Context, Reader>& scanner)
        {
            // Match the number of hashes, rather than the upstream approximation.
            std::size_t hashes = 0;
            while (scanner.branch(dsl::lit_c<'#'>))
                ++hashes;
            scanner.parse(dsl::lit_c<'"'>);
            while (scanner)
            {
                if (scanner.branch(dsl::lit_c<'"'>))
                {
                    std::size_t closing = 0;
                    while (closing < hashes && scanner.branch(dsl::lit_c<'#'>))
                        ++closing;
                    if (closing == hashes)
                        return true;
                }
                else if (scanner.is_at_eof())
                {
                    scanner.fatal_error("unterminated raw string", scanner.begin(),
                                        scanner.position());
                    break;
                }
                else
                    scanner.parse(dsl::code_point);
            }
            return lexy::scan_failed;
        }
    };
    struct quoted_string : lexy::token_production
    {
        static constexpr auto rule = [] {
            auto hex = dsl::digit<dsl::hex>;
            auto unicode
                = dsl::lit_c<'u'> >> dsl::lit_c<'{'> + hex
                                         + dsl::if_(hex >> dsl::if_(
                                                        hex >> dsl::if_(
                                                            hex >> dsl::if_(hex >> dsl::if_(hex)))))
                                         + dsl::lit_c<'}'>;
            auto escape = dsl::backslash >> (LEXY_ASCII_ONE_OF("\"\\/bfnrt") | unicode);
            return dsl::lit_c<'"'> >> dsl::while_(escape
                                                  | (dsl::code_point - LEXY_ASCII_ONE_OF("\"\\")))
                                          + dsl::lit_c<'"'>;
        }();
    };
    constexpr auto raw_start = dsl::token(dsl::lit_c<'r'> + LEXY_ASCII_ONE_OF("#\""));
    struct string
    {
        static constexpr auto rule
            = (dsl::peek(raw_start) >> dsl::p<raw_string>) | dsl::p<quoted_string>;
    };
    constexpr auto id_char
        = dsl::code_point - dsl::unicode::space - LEXY_ASCII_ONE_OF("\\/(){}<>;[]=,\"");
    constexpr auto bare_id
        = dsl::identifier(id_char - dsl::ascii::digit, id_char)
              .reserve_prefix(LEXY_LIT("+0"), LEXY_LIT("+1"), LEXY_LIT("+2"), LEXY_LIT("+3"),
                              LEXY_LIT("+4"), LEXY_LIT("+5"), LEXY_LIT("+6"), LEXY_LIT("+7"),
                              LEXY_LIT("+8"), LEXY_LIT("+9"), LEXY_LIT("-0"), LEXY_LIT("-1"),
                              LEXY_LIT("-2"), LEXY_LIT("-3"), LEXY_LIT("-4"), LEXY_LIT("-5"),
                              LEXY_LIT("-6"), LEXY_LIT("-7"), LEXY_LIT("-8"), LEXY_LIT("-9"));
    struct identifier
    {
        static constexpr auto rule = dsl::p<string> | bare_id;
    };
    struct type
    {
        static constexpr auto rule = dsl::lit_c<'('> >> dsl::p<identifier> + dsl::lit_c<')'>;
    };
    struct number : lexy::token_production
    {
        static constexpr auto rule = [] {
            auto decimal = dsl::ascii::digit + dsl::while_(dsl::ascii::digit / dsl::lit_c<'_'>);
            auto hex = dsl::digit<dsl::hex> + dsl::while_(dsl::digit<dsl::hex> / dsl::lit_c<'_'>);
            auto octal
                = dsl::digit<dsl::octal> + dsl::while_(dsl::digit<dsl::octal> / dsl::lit_c<'_'>);
            auto binary
                = dsl::digit<dsl::binary> + dsl::while_(dsl::digit<dsl::binary> / dsl::lit_c<'_'>);
            return dsl::peek(LEXY_ASCII_ONE_OF("+-") / dsl::ascii::digit)
                   >> dsl::if_(LEXY_ASCII_ONE_OF("+-"))
                          + ((LEXY_LIT("0x") >> hex) | (LEXY_LIT("0o") >> octal)
                             | (LEXY_LIT("0b") >> binary)
                             | dsl::else_ >> decimal + dsl::if_(dsl::period >> decimal)
                                                 + dsl::if_(LEXY_ASCII_ONE_OF("eE")
                                                            >> dsl::if_(LEXY_ASCII_ONE_OF("+-"))
                                                                   + decimal));
        }();
    };
    struct keyword
    {
        static constexpr auto rule = LEXY_KEYWORD("true", bare_id) | LEXY_KEYWORD("false", bare_id)
                                     | LEXY_KEYWORD("null", bare_id);
    };
    struct value
    {
        static constexpr auto rule
            = dsl::if_(dsl::p<type>) + (dsl::p<string> | dsl::p<number> | dsl::p<keyword>);
    };
    struct node_field
    {
        using scan_result          = lexy::scan_result<void>;
        static constexpr auto rule = dsl::scan;
        template <typename Context, typename Reader>
        static lexy::scan_result<void> scan(lexy::rule_scanner<Context, Reader>& scanner)
        {
            if (scanner.peek(dsl::lit_c<'('>)
                || scanner.peek(dsl::ascii::digit
                                | dsl::token((dsl::lit_c<'+'> / dsl::lit_c<'-'>)+dsl::ascii::digit))
                || scanner.peek(dsl::p<keyword>))
                scanner.parse(dsl::p<value>);
            else if (scanner.peek(dsl::lit_c<'"'> | raw_start))
            {
                scanner.parse(dsl::p<string>);
                if (scanner.branch(dsl::lit_c<'='>))
                    scanner.parse(dsl::p<value>);
            }
            else
                scanner.parse(dsl::p<identifier> + dsl::lit_c<'='> + dsl::p<value>);
            return bool(scanner);
        }
    };
    struct nodes;
    struct node_children
    {
        static constexpr auto rule = dsl::lit_c<'{'> >> dsl::recurse<nodes> + dsl::lit_c<'}'>;
    };
    struct node
    {
        using scan_result          = lexy::scan_result<void>;
        static constexpr auto rule = dsl::scan;
        template <typename Context, typename Reader>
        static lexy::scan_result<void> scan(lexy::rule_scanner<Context, Reader>& scanner)
        {
            scanner.parse(dsl::if_(LEXY_LIT("/-") >> dsl::while_(node_space))
                          + dsl::if_(dsl::p<type>) + dsl::p<identifier>);
            while (scanner)
            {
                bool separated = scanner.branch(dsl::while_one(node_space));
                bool commented = scanner.branch(LEXY_LIT("/-") >> dsl::while_(node_space));
                if (scanner.branch(dsl::p<node_children>))
                {
                    scanner.parse(dsl::while_(node_space));
                    break;
                }
                if (!commented
                    && scanner.peek(newline | dsl::semicolon | dsl::eof | LEXY_LIT("//")))
                    break;
                if (!separated)
                {
                    scanner.fatal_error("expected space before node field", scanner.position());
                    return lexy::scan_failed;
                }
                scanner.parse(dsl::p<node_field>);
            }
            scanner.parse(dsl::p<single_line_comment> | newline | dsl::semicolon | dsl::eof);
            return bool(scanner);
        }
    };
    struct nodes
    {
        static constexpr auto rule = dsl::while_(line_space)
                                     + dsl::while_(dsl::peek_not(dsl::lit_c<'}'> | dsl::eof)
                                                   >> dsl::recurse<node> + dsl::while_(line_space));
    };
    struct document
    {
        static constexpr auto rule = dsl::p<nodes> + dsl::eof;
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
