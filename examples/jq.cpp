// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Parse jq using expression productions for precedence, recursive queries, and
// string interpolation. Supports functions, bindings, destructuring, control flow,
// modules, and imports. Object values exclude comma expressions unless parenthesized.
// This example builds a lossless syntax tree without evaluating queries or loading
// modules. Decimal fractions/exponents and @format strings are supported.
//
// Usage: build/examples/jq <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_jq; test target: lexy_test_jq.
// Reference: https://github.com/flurie/tree-sitter-jq/tree/c204e36d2c3c6fce1f57950b12cabcc24e5cc4d9
// Located through Nix: tree-sitter-grammars.tree-sitter-jq.src
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

    constexpr auto word
        = dsl::identifier(dsl::ascii::alpha_underscore, dsl::ascii::alnum / dsl::lit_c<'_'>);
    constexpr auto reserved
        = dsl::literal_set(LEXY_LIT("module"), LEXY_LIT("import")) / LEXY_LIT("include")
          / LEXY_LIT("def") / LEXY_LIT("as") / LEXY_LIT("if") / LEXY_LIT("then") / LEXY_LIT("else")
          / LEXY_LIT("elif") / LEXY_LIT("end") / LEXY_LIT("and") / LEXY_LIT("or")
          / LEXY_LIT("reduce") / LEXY_LIT("foreach") / LEXY_LIT("try") / LEXY_LIT("catch")
          / LEXY_LIT("label") / LEXY_LIT("break");
#define JQ_KW(Str) LEXY_KEYWORD(Str, word)
    struct query;
    struct expression;
    struct object_value;
    struct term;
    struct binding_pattern;
    struct identifier : lexy::token_production
    {
        static constexpr auto rule
            = word.pattern() >> dsl::while_(LEXY_LIT("::") >> word.pattern());
    };
    struct variable : lexy::token_production
    {
        static constexpr auto rule = dsl::lit_c<'$'> >> word;
    };
    struct number : lexy::token_production
    {
        // Also accept decimal fractions and exponents, missing in the source grammar.
        static constexpr auto rule
            = dsl::digits<> >> dsl::if_(dsl::period >> dsl::digits<>)
                                   + dsl::if_(LEXY_ASCII_ONE_OF("eE")
                                              >> dsl::if_(LEXY_ASCII_ONE_OF("+-")) + dsl::digits<>);
    };
    struct interpolation_query : lexy::transparent_production
    {
        static constexpr auto whitespace
            = dsl::ascii::space | (dsl::lit_c<'#'> >> dsl::until(dsl::newline).or_eof());
        static constexpr auto rule = dsl::recurse<query>;
    };
    struct string_interpolation : lexy::token_production
    {
        // Query whitespace stops before ')'; following spaces belong to the string.
        static constexpr auto rule
            = LEXY_LIT("\\(") >> dsl::p<interpolation_query> + dsl::lit_c<')'>;
    };
    struct string : lexy::token_production
    {
        static constexpr auto rule
            = dsl::lit_c<'"'> >> dsl::while_(
                                     (dsl::peek(LEXY_LIT("\\(")) >> dsl::p<string_interpolation>)
                                     | (dsl::backslash >> (LEXY_ASCII_ONE_OF("\"\\/bfnrt")
                                                           | (dsl::lit_c<'u'> >> dsl::times<4>(
                                                                  dsl::digit<dsl::hex>))))
                                     | (dsl::code_point - LEXY_ASCII_ONE_OF("\"\\\n")))
                                     + dsl::lit_c<'"'>;
    };
    struct format : lexy::token_production
    {
        static constexpr auto rule = dsl::lit_c<'@'> >> word;
    };
    struct suffix
    {
        static constexpr auto rule
            = dsl::lit_c<'['> >> dsl::if_(
                                     dsl::peek_not(dsl::lit_c<']'>)
                                     >> ((dsl::colon >> dsl::recurse<query>)
                                         | dsl::else_
                                               >> dsl::recurse<query>
                                                      + dsl::if_(dsl::colon >> dsl::if_(
                                                                     dsl::peek_not(dsl::lit_c<']'>)
                                                                     >> dsl::recurse<query>))))
                                     + dsl::lit_c<']'>;
    };
    struct index
    {
        static constexpr auto rule
            = dsl::period >> (dsl::p<identifier> | dsl::p<string> | dsl::p<suffix>);
    };
    struct function_call
    {
        static constexpr auto rule
            = dsl::peek(word.reserve(reserved))
              >> dsl::p<identifier>
                     + dsl::if_(dsl::lit_c<'('> >> dsl::list(dsl::recurse<query>,
                                                             dsl::sep(dsl::semicolon))
                                                       + dsl::lit_c<')'>);
    };
    struct array
    {
        static constexpr auto rule
            = dsl::lit_c<'['> >> dsl::if_(dsl::peek_not(dsl::lit_c<']'>) >> dsl::recurse<query>)
                                     + dsl::lit_c<']'>;
    };
    struct object_key
    {
        static constexpr auto rule = dsl::p<identifier> | dsl::p<variable> | dsl::p<string>
                                     | (dsl::lit_c<'('> >> dsl::recurse<query> + dsl::lit_c<')'>);
    };
    struct object_entry
    {
        static constexpr auto rule
            = dsl::p<object_key> >> dsl::if_(dsl::colon >> dsl::recurse<object_value>);
    };
    struct object
    {
        static constexpr auto rule
            = dsl::lit_c<'{'> >> dsl::opt(dsl::list(dsl::p<object_entry>,
                                                    dsl::trailing_sep(dsl::comma)))
                                     + dsl::lit_c<'}'>;
    };
    struct pattern_entry
    {
        static constexpr auto rule
            = (dsl::p<variable> >> dsl::if_(dsl::colon >> dsl::recurse<binding_pattern>))
              | (dsl::p<object_key> >> dsl::colon + dsl::recurse<binding_pattern>);
    };
    struct binding_pattern
    {
        static constexpr auto rule
            = dsl::p<variable>
              | (dsl::lit_c<'['> >> dsl::list(dsl::recurse<binding_pattern>, dsl::sep(dsl::comma))
                                        + dsl::lit_c<']'>)
              | (dsl::lit_c<'{'> >> dsl::list(dsl::p<pattern_entry>, dsl::sep(dsl::comma))
                                        + dsl::lit_c<'}'>);
    };
    struct binding
    {
        static constexpr auto rule
            = JQ_KW("as") >> dsl::p<binding_pattern>
                                 + dsl::while_(LEXY_LIT("?//") >> dsl::p<binding_pattern>);
    };
    struct conditional
    {
        static constexpr auto rule
            = JQ_KW("if") >> dsl::recurse<query> + JQ_KW("then") + dsl::recurse<query>
                                 + dsl::while_(JQ_KW("elif") >> dsl::recurse<query> + JQ_KW("then")
                                                                    + dsl::recurse<query>)
                                 + dsl::if_(JQ_KW("else") >> dsl::recurse<query>) + JQ_KW("end");
    };
    struct try_catch
    {
        static constexpr auto rule
            = JQ_KW("try") >> dsl::recurse<query> + dsl::if_(JQ_KW("catch") >> dsl::recurse<query>);
    };
    struct reduce
    {
        static constexpr auto rule
            = JQ_KW("reduce") >> dsl::while_(LEXY_ASCII_ONE_OF("+-")) + dsl::recurse<term>
                                     + JQ_KW("as") + dsl::p<binding_pattern> + dsl::lit_c<'('>
                                     + dsl::recurse<query> + dsl::semicolon + dsl::recurse<query>
                                     + dsl::lit_c<')'>;
    };
    struct foreach
    {
        static constexpr auto rule
            = JQ_KW("foreach")
              >> dsl::recurse<term> + JQ_KW("as") + dsl::p<binding_pattern> + dsl::lit_c<'('>
                     + dsl::recurse<query> + dsl::semicolon + dsl::recurse<query>
                     + dsl::if_(dsl::semicolon >> dsl::recurse<query>) + dsl::lit_c<')'>;
    };
    struct label
    {
        static constexpr auto rule
            = JQ_KW("label") >> dsl::p<variable> + dsl::lit_c<'|'> + dsl::recurse<query>;
    };
    struct term
    {
        static constexpr auto rule = [] {
            auto atom
                = dsl::p<conditional> | dsl::p<try_catch> | dsl::p<reduce> | dsl::p<foreach>
                  | dsl::p<label> | (JQ_KW("break") >> dsl::p<variable>) | LEXY_LIT("..")
                  | (dsl::period >> dsl::if_(dsl::p<identifier> | dsl::p<string> | dsl::p<suffix>))
                  | (dsl::p<format> >> dsl::if_(dsl::p<string>)) | dsl::p<string> | dsl::p<variable>
                  | dsl::p<number> | dsl::p<array> | dsl::p<object> | dsl::p<function_call>
                  | (dsl::lit_c<'('> >> dsl::recurse<query> + dsl::lit_c<')'>);
            return atom >> dsl::while_(dsl::p<index> | dsl::p<suffix>
                                       | dsl::not_followed_by(dsl::lit_c<'?'>, dsl::lit_c<'/'>));
        }();
    };
    struct expression : lexy::expression_production
    {
        static constexpr auto atom = dsl::p<term> + dsl::if_(dsl::p<binding>);
        struct unary : dsl::prefix_op
        {
            static constexpr auto op
                = dsl::op<void>(dsl::lit_c<'+'>) / dsl::op<void>(dsl::lit_c<'-'>);
            using operand = dsl::atom;
        };
        struct product : dsl::infix_op_left
        {
            static constexpr auto op
                = dsl::op<void>(dsl::not_followed_by(dsl::lit_c<'*'>, dsl::lit_c<'='>))
                  / dsl::op<void>(dsl::not_followed_by(dsl::lit_c<'/'>, LEXY_ASCII_ONE_OF("/=")))
                  / dsl::op<void>(dsl::not_followed_by(dsl::lit_c<'%'>, dsl::lit_c<'='>));
            using operand = unary;
        };
        struct sum : dsl::infix_op_left
        {
            static constexpr auto op
                = dsl::op<void>(dsl::not_followed_by(dsl::lit_c<'+'>, dsl::lit_c<'='>))
                  / dsl::op<void>(dsl::not_followed_by(dsl::lit_c<'-'>, dsl::lit_c<'='>));
            using operand = product;
        };
        struct compare : dsl::infix_op_single
        {
            static constexpr auto op
                = dsl::op<void>(LEXY_LIT("==")) / dsl::op<void>(LEXY_LIT("!="))
                  / dsl::op<void>(LEXY_LIT(">=")) / dsl::op<void>(LEXY_LIT("<="))
                  / dsl::op<void>(dsl::lit_c<'>'>) / dsl::op<void>(dsl::lit_c<'<'>);
            using operand = sum;
        };
        struct boolean_and : dsl::infix_op_left
        {
            static constexpr auto op = dsl::op<void>(JQ_KW("and"));
            using operand            = compare;
        };
        struct boolean_or : dsl::infix_op_left
        {
            static constexpr auto op = dsl::op<void>(JQ_KW("or"));
            using operand            = boolean_and;
        };
        struct update : dsl::infix_op_single
        {
            static constexpr auto op
                = dsl::op<void>(LEXY_LIT("|=")) / dsl::op<void>(LEXY_LIT("+="))
                  / dsl::op<void>(LEXY_LIT("-=")) / dsl::op<void>(LEXY_LIT("*="))
                  / dsl::op<void>(LEXY_LIT("/=")) / dsl::op<void>(LEXY_LIT("//="))
                  / dsl::op<void>(LEXY_LIT("%="))
                  / dsl::op<void>(dsl::not_followed_by(dsl::lit_c<'='>, dsl::lit_c<'='>));
            using operand = boolean_or;
        };
        struct alternative : dsl::infix_op_right
        {
            static constexpr auto op
                = dsl::op<void>(dsl::not_followed_by(LEXY_LIT("//"), dsl::lit_c<'='>));
            using operand = update;
        };
        struct comma : dsl::infix_op_left
        {
            static constexpr auto op = dsl::op<void>(dsl::comma);
            using operand            = alternative;
        };
        struct pipe : dsl::infix_op_right
        {
            static constexpr auto op
                = dsl::op<void>(dsl::not_followed_by(dsl::lit_c<'|'>, dsl::lit_c<'='>));
            using operand = comma;
        };
        using operation = pipe;
    };
    struct object_value : lexy::expression_production
    {
        // Commas separate object entries; parentheses restore the full expression grammar.
        static constexpr auto atom = expression::atom;
        struct pipe : dsl::infix_op_right
        {
            static constexpr auto op = expression::pipe::op;
            using operand            = expression::alternative;
        };
        using operation = pipe;
    };
    struct function_definition
    {
        static constexpr auto rule
            = JQ_KW("def")
              >> dsl::p<identifier>
                     + dsl::if_(dsl::lit_c<'('> >> dsl::list(dsl::p<variable> | dsl::p<identifier>,
                                                             dsl::sep(dsl::semicolon))
                                                       + dsl::lit_c<')'>)
                     + dsl::colon + dsl::recurse<query> + dsl::semicolon;
    };
    struct query
    {
        static constexpr auto rule = dsl::while_(dsl::p<function_definition>) + dsl::p<expression>;
    };
    struct constant_value;
    struct constant_string : lexy::token_production
    {
        static constexpr auto rule
            = dsl::lit_c<'"'> >> dsl::while_((dsl::backslash
                                              >> (LEXY_ASCII_ONE_OF("\"\\/bfnrt")
                                                  | (dsl::lit_c<'u'> >> dsl::times<4>(
                                                         dsl::digit<dsl::hex>))))
                                             | (dsl::code_point - LEXY_ASCII_ONE_OF("\"\\\n")))
                                     + dsl::lit_c<'"'>;
    };
    struct constant_object
    {
        static constexpr auto rule
            = dsl::lit_c<'{'> >> dsl::opt(dsl::list((dsl::p<constant_string> | dsl::p<identifier>)
                                                        >> dsl::colon
                                                               + dsl::recurse<constant_value>,
                                                    dsl::sep(dsl::comma)))
                                     + dsl::lit_c<'}'>;
    };
    struct constant_array
    {
        static constexpr auto rule
            = dsl::lit_c<'['> >> dsl::if_(dsl::peek_not(dsl::lit_c<']'>)
                                          >> dsl::list(dsl::recurse<constant_value>,
                                                       dsl::sep(dsl::comma)))
                                     + dsl::lit_c<']'>;
    };
    struct constant_value
    {
        static constexpr auto rule = dsl::p<constant_object> | dsl::p<constant_array>
                                     | dsl::p<constant_string> | dsl::p<number>
                                     | (LEXY_ASCII_ONE_OF("+-") >> dsl::p<number>) | JQ_KW("true")
                                     | JQ_KW("false") | JQ_KW("null");
    };
    struct import
    {
        static constexpr auto rule
            = (JQ_KW("import")
               >> dsl::p<string> + JQ_KW("as")
                      + (dsl::p<variable> | dsl::p<identifier>)+dsl::if_(dsl::p<constant_object>)
                      + dsl::semicolon)
              | (JQ_KW("include") >> dsl::p<string>
                                         + dsl::if_(dsl::p<constant_object>) + dsl::semicolon);
    };
    struct document
    {
        static constexpr auto whitespace
            = dsl::ascii::space | (dsl::lit_c<'#'> >> dsl::until(dsl::newline).or_eof());
        using scan_result = lexy::scan_result<void>;
        static constexpr auto rule
            = dsl::if_(JQ_KW("module") >> dsl::p<constant_object> + dsl::semicolon)
              + dsl::while_(dsl::p<import>) + dsl::scan + dsl::eof;
        template <typename Context, typename Reader>
        static scan_result scan(lexy::rule_scanner<Context, Reader>& scanner)
        {
            bool definitions = false;
            while (scanner.branch(dsl::p<function_definition>))
                definitions = true;
            if (!definitions || !scanner.is_at_eof())
                scanner.parse(dsl::p<expression>);
            return bool(scanner);
        }
    };
#undef JQ_KW
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
