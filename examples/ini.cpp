// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Parse INI into an owning document with global settings and ordered sections.
// Value callbacks preserve duplicate names and literal values. Only full-line comments
// are recognized; quotes, backslashes, and inline comment markers remain literal.
// Whitespace around keys and before values is removed; trailing value spaces remain.
// The executable prints settings with normalized spacing and omits comments.
//
// Usage: build/examples/ini <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_ini; test target: lexy_test_ini.
// Reference:
// https://github.com/justinmk/tree-sitter-ini/tree/e4018b5176132b4f3c5d6e61cea383f42288d0f5 Located
// through Nix: tree-sitter-grammars.tree-sitter-ini.src Nixpkgs revision:
// 8edc0c72e3a38faf5434e40d1f19431125fe4b30.

#include <cstdio>
#include <string>
#include <vector>

#include <lexy/action/parse.hpp>
#include <lexy/callback.hpp>
#include <lexy/dsl.hpp>
#include <lexy/input/file.hpp>
#include <lexy_ext/report_error.hpp>

namespace
{
namespace ast
{
    struct setting
    {
        std::string name;
        std::string value;
    };
    struct section
    {
        std::string          name;
        std::vector<setting> settings;
    };
    struct document
    {
        // Keep order and duplicates; choosing an overwrite policy belongs to the application.
        std::vector<setting> globals;
        std::vector<section> sections;
    };
} // namespace ast

namespace grammar
{
    namespace dsl = lexy::dsl;

    // Whitespace is explicit so a missing '=' cannot make a setting continue on the next line.
    constexpr auto ws             = dsl::while_(dsl::ascii::blank);
    constexpr auto line_character = dsl::code_point - dsl::ascii::newline;
    constexpr auto tail           = dsl::token(dsl::while_(line_character));

    constexpr auto trivia = [] {
        auto comment = LEXY_ASCII_ONE_OF(";#") >> tail + dsl::eol;
        // Each successful iteration consumes a newline or a comment marker, even at EOF.
        return dsl::while_(
            dsl::token(ws + (comment | dsl::newline)).kind<lexy::whitespace_token_kind>);
    }();

    constexpr auto key_character
        = dsl::code_point - dsl::unicode::space - LEXY_ASCII_ONE_OF(";#=[");

    struct setting_name : lexy::token_production
    {
        static constexpr auto rule = [] {
            // Spaces are permitted inside names, but the spaces before '=' are not part of
            // the name. Look ahead for the next name character before consuming a space run.
            auto word   = dsl::while_one(key_character);
            auto spaces = dsl::while_one(dsl::lit_c<' '>);
            return dsl::capture(
                dsl::token(word + dsl::while_(dsl::peek(spaces + key_character) >> spaces + word)));
        }();
        static constexpr auto value = lexy::as_string<std::string>;
    };

    struct setting_value : lexy::token_production
    {
        // Empty values are allowed. Quotes, '=', '#', ';', and backslashes are literal text.
        static constexpr auto rule  = dsl::capture(tail);
        static constexpr auto value = lexy::as_string<std::string>;
    };

    struct setting
    {
        static constexpr auto rule
            = dsl::peek(ws + key_character) >> ws + dsl::p<setting_name> + ws + dsl::lit_c<'='>
                                                   + ws + dsl::p<setting_value> + dsl::eol + trivia;
        static constexpr auto value = lexy::construct<ast::setting>;
    };

    struct settings
    {
        static constexpr auto rule  = dsl::opt(dsl::list(dsl::p<setting>));
        static constexpr auto value = lexy::as_list<std::vector<ast::setting>>;
    };

    struct section_name : lexy::token_production
    {
        static constexpr auto rule
            = dsl::capture(dsl::token(dsl::while_one(line_character - LEXY_ASCII_ONE_OF("[]"))));
        static constexpr auto value = lexy::as_string<std::string>;
    };

    struct section
    {
        static constexpr auto rule
            = dsl::peek(ws + dsl::lit_c<'['>) >> ws + dsl::lit_c<'['> + dsl::p<section_name>
                                                     + dsl::lit_c<']'>
                                                     + ws + dsl::eol + trivia + dsl::p<settings>;
        static constexpr auto value = lexy::construct<ast::section>;
    };

    struct sections
    {
        static constexpr auto rule  = dsl::opt(dsl::list(dsl::p<section>));
        static constexpr auto value = lexy::as_list<std::vector<ast::section>>;
    };

    struct document
    {
        static constexpr auto rule  = trivia + dsl::p<settings> + dsl::p<sections> + ws + dsl::eof;
        static constexpr auto value = lexy::construct<ast::document>;
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
    auto result
        = lexy::parse<grammar::document>(file.buffer(), lexy_ext::report_error.path(argv[1]));
    if (!result)
        return 2;

    auto print_settings = [](const std::vector<ast::setting>& settings) {
        for (const auto& setting : settings)
            std::printf("%s = %s\n", setting.name.c_str(), setting.value.c_str());
    };
    print_settings(result.value().globals);
    for (const auto& section : result.value().sections)
    {
        std::printf("[%s]\n", section.name.c_str());
        print_settings(section.settings);
    }
}
#endif
