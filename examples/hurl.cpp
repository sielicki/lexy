// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Parse Hurl request/response files by composing line-oriented and embedded
// grammars: sections, captures, assertions, templates, JSON, XML, and text bodies.
// XML closing names must match; DTDs and entity expansion are unsupported.
// The executable builds a lossless tree without sending requests, reading body files,
// evaluating templates, or running assertions.
//
// Usage: build/examples/hurl <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_hurl; test target: lexy_test_hurl.
// Reference: https://github.com/pfeiferj/tree-sitter-hurl/tree/v1.7.0
// Located through Nix: tree-sitter-grammars.tree-sitter-hurl.src
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
        = dsl::identifier(dsl::ascii::alpha, dsl::ascii::alnum / LEXY_ASCII_ONE_OF("_-"));
#define HURL_KW(Str) LEXY_KEYWORD(Str, word)
    constexpr auto comment   = dsl::lit_c<'#'> >> dsl::until(dsl::newline).or_eof();
    constexpr auto trivia    = dsl::while_(dsl::newline | comment);
    constexpr auto end_line  = (comment | dsl::eol) + trivia;
    constexpr auto line_char = dsl::code_point - LEXY_ASCII_ONE_OF("\r\n");
    struct template_expression;
    struct quoted_string : lexy::token_production
    {
        static constexpr auto rule
            = dsl::lit_c<'"'> >> dsl::while_(
                                     (dsl::peek(LEXY_LIT("{{"))
                                      >> dsl::recurse<template_expression>)
                                     | (dsl::backslash
                                        >> ((dsl::lit_c<
                                                 'u'> >> ((dsl::
                                                               lit_c<'{'> >> dsl::times<4>(
                                                                                 dsl::digit<
                                                                                     dsl::hex>)
                                                                                 + dsl::lit_c<'}'>)
                                                          | dsl::else_ >> dsl::times<4>(
                                                                dsl::digit<dsl::hex>)))
                                            | (line_char - dsl::lit_c<'u'>)))
                                     | (line_char - LEXY_ASCII_ONE_OF("\"\\")))
                                     + dsl::lit_c<'"'>;
    };
    struct regex : lexy::token_production
    {
        static constexpr auto rule
            = dsl::lit_c<'/'> >> dsl::while_((dsl::backslash >> line_char)
                                             | (line_char - LEXY_ASCII_ONE_OF("/\\")))
                                     + dsl::lit_c<'/'>;
    };
    struct number : lexy::token_production
    {
        static constexpr auto rule
            = dsl::peek(dsl::ascii::digit / dsl::lit_c<'-'>)
              >> dsl::if_(dsl::lit_c<'-'>) + dsl::digits<> + dsl::if_(dsl::period >> dsl::digits<>)
                     + dsl::if_(LEXY_ASCII_ONE_OF("eE")
                                >> dsl::if_(LEXY_ASCII_ONE_OF("+-")) + dsl::digits<>);
    };
    struct filter
    {
        static constexpr auto rule
            = ((HURL_KW("regex") | HURL_KW("xpath")) >> (dsl::p<quoted_string> | dsl::p<regex>))
              | (HURL_KW("replace")
                 >> (dsl::p<quoted_string> | dsl::p<regex>)+dsl::p<quoted_string>)
              | (HURL_KW("split") >> dsl::p<quoted_string>) | (HURL_KW("nth") >> dsl::p<number>)
              | HURL_KW("count") | HURL_KW("urlEncode") | HURL_KW("urlDecode") | HURL_KW("toInt")
              | HURL_KW("htmlEscape") | HURL_KW("htmlUnescape") | HURL_KW("daysAfterNow")
              | HURL_KW("daysBeforeNow") | HURL_KW("decode") | HURL_KW("format")
              | HURL_KW("toDate");
    };
    struct template_expression
    {
        static constexpr auto whitespace = dsl::ascii::blank;
        static constexpr auto rule
            = LEXY_LIT("{{") + word + dsl::while_(dsl::p<filter>) + LEXY_LIT("}}");
    };
    struct value_string : lexy::token_production
    {
        static constexpr auto rule = dsl::while_one(
            (dsl::peek(LEXY_LIT("{{")) >> dsl::p<template_expression>)
            | (dsl::backslash >> line_char) | (line_char - LEXY_ASCII_ONE_OF("#\\")));
    };
    constexpr auto key_char = dsl::ascii::alnum / LEXY_ASCII_ONE_OF("_-.[]@$");
    struct key_string : lexy::token_production
    {
        static constexpr auto rule
            = dsl::while_one((dsl::peek(LEXY_LIT("{{")) >> dsl::p<template_expression>)
                             | (dsl::backslash >> LEXY_ASCII_ONE_OF("#\"")) | key_char);
    };
    constexpr auto key_start = [] {
        // Inspect delimiters only: lookahead must not recursively parse template filters.
        auto template_key
            = LEXY_LIT("{{")
              >> dsl::while_(dsl::peek_not(LEXY_LIT("}}")) >> line_char) + LEXY_LIT("}}");
        return dsl::token(dsl::while_one(template_key | key_char | (dsl::backslash >> line_char))
                          + dsl::while_(dsl::ascii::blank) + dsl::colon);
    }();
    struct header
    {
        static constexpr auto rule = dsl::peek(key_start) >> dsl::p<key_string> + dsl::colon
                                                                 + dsl::if_(dsl::p<value_string>)
                                                                 + end_line;
    };
    struct query
    {
        static constexpr auto rule
            = HURL_KW("status") | HURL_KW("url") | HURL_KW("body") | HURL_KW("duration")
              | HURL_KW("bytes") | HURL_KW("sha256") | HURL_KW("md5")
              | ((HURL_KW("header") | HURL_KW("cookie") | HURL_KW("xpath") | HURL_KW("jsonpath")
                  | HURL_KW("variable"))
                 >> dsl::p<quoted_string>)
              | (HURL_KW("regex") >> (dsl::p<quoted_string> | dsl::p<regex>));
    };
    struct binary_literal : lexy::token_production
    {
        static constexpr auto rule
            = (LEXY_LIT("hex,") >> dsl::while_(dsl::digit<dsl::hex>) + dsl::semicolon)
              | (LEXY_LIT("base64,")
                 >> dsl::while_one(dsl::ascii::alnum / LEXY_ASCII_ONE_OF("+/=,-\r\n"))
                        + dsl::semicolon)
              | (LEXY_LIT("file,")
                 >> dsl::while_((dsl::peek(LEXY_LIT("{{")) >> dsl::p<template_expression>)
                                | (dsl::backslash >> line_char)
                                | (line_char - LEXY_ASCII_ONE_OF(";#\\")))
                        + dsl::semicolon);
    };
    struct multiline_string : lexy::token_production
    {
        static constexpr auto rule
            = LEXY_LIT("```")
              >> dsl::while_(line_char) + dsl::newline
                     + dsl::while_(dsl::peek_not(LEXY_LIT("```"))
                                   >> ((dsl::peek(LEXY_LIT("{{")) >> dsl::p<template_expression>)
                                       | (dsl::backslash >> dsl::code_point) | dsl::code_point))
                     + LEXY_LIT("```");
    };
    struct oneline_string : lexy::token_production
    {
        static constexpr auto rule
            = dsl::lit_c<'`'> >> dsl::while_((dsl::peek(LEXY_LIT("{{"))
                                              >> dsl::p<template_expression>)
                                             | (dsl::backslash >> line_char)
                                             | (line_char - LEXY_ASCII_ONE_OF("`\\")))
                                     + dsl::lit_c<'`'>;
    };
    struct predicate_value
    {
        static constexpr auto rule
            = (dsl::peek(LEXY_LIT("{{")) >> dsl::p<template_expression>) | dsl::p<quoted_string>
              | dsl::p<number> | HURL_KW("true") | HURL_KW("false") | HURL_KW("null")
              | dsl::p<binary_literal> | dsl::p<multiline_string>;
    };
    struct predicate
    {
        static constexpr auto rule
            = dsl::if_(HURL_KW("not"))
              + (HURL_KW("exists") | HURL_KW("isInteger") | HURL_KW("isFloat")
                 | HURL_KW("isBoolean") | HURL_KW("isString") | HURL_KW("isCollection")
                 | HURL_KW("isNumber") | HURL_KW("isIsoDate") | HURL_KW("isEmpty")
                 | HURL_KW("isIpv4") | HURL_KW("isIpv6") | HURL_KW("isUuid")
                 | ((dsl::literal_set(LEXY_LIT("=="), LEXY_LIT("!=")) / LEXY_LIT(">=")
                     / LEXY_LIT("<=") / LEXY_LIT(">") / LEXY_LIT("<"))
                    >> dsl::p<predicate_value>)
                 | ((HURL_KW("equals") | HURL_KW("notEquals") | HURL_KW("greaterThan")
                     | HURL_KW("greaterThanOrEquals") | HURL_KW("lessThan")
                     | HURL_KW("lessThanOrEquals") | HURL_KW("startsWith") | HURL_KW("endsWith")
                     | HURL_KW("contains") | HURL_KW("includes"))
                    >> dsl::p<predicate_value>)
                 | (HURL_KW("matches") >> (dsl::p<regex> | dsl::p<quoted_string>)));
    };
    struct assertion
    {
        static constexpr auto rule
            = dsl::p<query> >> dsl::while_(dsl::p<filter>) + dsl::p<predicate> + end_line;
    };
    struct capture
    {
        static constexpr auto rule
            = dsl::peek(key_start) >> dsl::p<key_string> + dsl::colon + dsl::p<query>
                                          + dsl::while_(dsl::p<filter>) + end_line;
    };
    struct option
    {
        static constexpr auto rule = [] {
            auto boolean_key = HURL_KW("compressed") | HURL_KW("location")
                               | HURL_KW("location-trusted") | HURL_KW("http1.0") | HURL_KW("http2")
                               | HURL_KW("http3") | HURL_KW("insecure") | HURL_KW("ipv4")
                               | HURL_KW("ipv6") | HURL_KW("netrc") | HURL_KW("netrc-optional")
                               | HURL_KW("path-as-is") | HURL_KW("skip") | HURL_KW("verbose")
                               | HURL_KW("very-verbose");
            auto integer_key = HURL_KW("max-redirs") | HURL_KW("retry-max-count")
                               | HURL_KW("limit-rate") | HURL_KW("max-time") | HURL_KW("repeat");
            auto duration_key
                = HURL_KW("connect-timeout") | HURL_KW("delay") | HURL_KW("retry-interval");
            auto string_key = HURL_KW("cacert") | HURL_KW("cert") | HURL_KW("aws-sigv4")
                              | HURL_KW("key") | HURL_KW("connect-to") | HURL_KW("header")
                              | HURL_KW("netrc-file") | HURL_KW("output") | HURL_KW("pinnedpubkey")
                              | HURL_KW("proxy") | HURL_KW("unix-socket") | HURL_KW("user");
            auto templated  = dsl::peek(LEXY_LIT("{{")) >> dsl::p<template_expression>;
            return ((boolean_key >> dsl::colon + (HURL_KW("true") | HURL_KW("false") | templated))
                    | (integer_key >> dsl::colon + (dsl::digits<> | templated))
                    | (duration_key >> dsl::colon
                                           + ((dsl::digits<> >> dsl::if_(
                                                   dsl::literal_set(LEXY_LIT("ms"), LEXY_LIT("s"))
                                                   / LEXY_LIT("m")))
                                              | templated))
                    | (HURL_KW("retry")
                       >> dsl::colon
                              + (dsl::digits<> | HURL_KW("true") | HURL_KW("false") | templated))
                    | (string_key >> dsl::colon + dsl::p<value_string>)
                    | (HURL_KW("variable") >> dsl::colon + word.pattern() + dsl::lit_c<'='>
                                                  + (dsl::p<quoted_string> | dsl::p<value_string>)))
                   >> end_line;
        }();
    };
    struct request_section
    {
        static constexpr auto rule
            = (LEXY_LIT("[BasicAuth]") >> end_line + dsl::p<header>)
              | (LEXY_LIT("[Options]") >> end_line + dsl::while_(dsl::p<option>))
              | ((dsl::literal_set(LEXY_LIT("[Query]"), LEXY_LIT("[QueryStringParams]"))
                  / LEXY_LIT("[Form]") / LEXY_LIT("[FormParams]") / LEXY_LIT("[Multipart]")
                  / LEXY_LIT("[MultipartFormData]") / LEXY_LIT("[Cookies]"))
                 >> end_line + dsl::while_(dsl::p<header>));
    };
    struct response_section
    {
        static constexpr auto rule
            = (LEXY_LIT("[Captures]") >> end_line + dsl::while_(dsl::p<capture>))
              | (LEXY_LIT("[Asserts]") >> end_line + dsl::while_(dsl::p<assertion>));
    };
    struct json_value;
    // Explicit whitespace keeps the newline after a body available to the Hurl grammar.
    constexpr auto json_space = dsl::whitespace(dsl::ascii::space);
    struct json_object : lexy::token_production
    {
        static constexpr auto rule
            = dsl::lit_c<'{'> >> json_space
                                     + dsl::opt(dsl::list(
                                         dsl::p<quoted_string> >> json_space + dsl::colon
                                                                      + json_space
                                                                      + dsl::recurse<json_value>
                                                                      + json_space,
                                         dsl::sep(dsl::comma >> json_space)))
                                     + dsl::lit_c<'}'>;
    };
    struct json_array : lexy::token_production
    {
        static constexpr auto rule
            = dsl::lit_c<'['> >> json_space
                                     + dsl::if_(dsl::peek_not(dsl::lit_c<']'>)
                                                >> dsl::list(dsl::recurse<json_value> + json_space,
                                                             dsl::sep(dsl::comma >> json_space)))
                                     + dsl::lit_c<']'>;
    };
    struct json_value
    {
        static constexpr auto rule
            = (dsl::peek(LEXY_LIT("{{")) >> dsl::p<template_expression>)
              | (dsl::peek(dsl::lit_c<'{'>) >> dsl::p<json_object>)
              | (dsl::peek(dsl::lit_c<'['>) >> dsl::p<json_array>) | dsl::p<quoted_string>
              | dsl::p<number> | HURL_KW("true") | HURL_KW("false") | HURL_KW("null");
    };
    struct xml : lexy::token_production
    {
        static constexpr auto rule = [] {
            auto name      = dsl::identifier(dsl::ascii::alpha_underscore,
                                             dsl::ascii::alnum / LEXY_ASCII_ONE_OF("_:-."));
            auto tag       = dsl::context_identifier<struct tag_id>(name);
            auto blank     = dsl::while_(dsl::ascii::space);
            auto attr_text = (dsl::lit_c<'"'> >> dsl::until(dsl::lit_c<'"'>))
                             | (dsl::lit_c<'\''> >> dsl::until(dsl::lit_c<'\''>));
            auto attr      = name.pattern() >> blank + dsl::lit_c<'='> + blank + attr_text + blank;
            auto instruction = LEXY_LIT("<?") >> dsl::until(LEXY_LIT("?>"));
            auto xml_comment = LEXY_LIT("<!--") >> dsl::until(LEXY_LIT("-->"));
            auto content     = instruction | xml_comment
                               | (dsl::peek(dsl::lit_c<'<'>) >> dsl::recurse<xml>)
                               | (dsl::peek(LEXY_LIT("{{")) >> dsl::p<template_expression>)
                               | (dsl::code_point - dsl::lit_c<'<'>);
            return tag.create() + dsl::while_(instruction >> blank) + dsl::lit_c<'<'>
                   + tag.capture() + blank + dsl::while_(attr)
                   + ((LEXY_LIT("/>"))
                      | (dsl::lit_c<'>'> >> dsl::while_(dsl::peek_not(LEXY_LIT("</")) >> content)
                                                + LEXY_LIT("</") + tag.rematch() + blank
                                                + dsl::lit_c<'>'>));
        }();
    };
    struct body
    {
        static constexpr auto rule
            = (dsl::p<multiline_string> | dsl::p<oneline_string> | dsl::p<binary_literal>
               | (dsl::peek(dsl::lit_c<'<'>) >> dsl::p<xml>) | dsl::p<json_value>)
              >> end_line;
    };
    struct method : lexy::token_production
    {
        static constexpr auto rule = dsl::identifier(dsl::ascii::upper).reserve(LEXY_LIT("HTTP"))
                                     + dsl::peek(dsl::ascii::blank | dsl::eol);
    };
    struct request
    {
        static constexpr auto rule
            = dsl::peek(dsl::ascii::upper) >> dsl::p<method> + dsl::if_(dsl::p<value_string>)
                                                  + end_line + dsl::while_(dsl::p<header>)
                                                  + dsl::while_(dsl::p<request_section>)
                                                  + dsl::if_(dsl::p<body>);
    };
    struct response
    {
        static constexpr auto rule
            = (dsl::literal_set(LEXY_LIT("HTTP/1.0"), LEXY_LIT("HTTP/1.1")) / LEXY_LIT("HTTP/2")
               / LEXY_LIT("HTTP/3") / LEXY_LIT("HTTP/*") / LEXY_LIT("HTTP"))
              >> (dsl::digits<> | dsl::lit_c<'*'>)+end_line + dsl::while_(dsl::p<header>)
                     + dsl::while_(dsl::p<response_section>) + dsl::if_(dsl::p<body>);
    };
    struct entry
    {
        static constexpr auto rule = dsl::p<request> >> dsl::if_(dsl::p<response>);
    };
    struct document
    {
        static constexpr auto whitespace = dsl::ascii::blank;
        static constexpr auto rule       = trivia + dsl::while_(dsl::p<entry>) + dsl::eof;
    };
#undef HURL_KW
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
