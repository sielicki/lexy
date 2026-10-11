// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/ini.cpp" // NOLINT

#include <doctest/doctest.h>
#include <lexy/action/match.hpp>
#include <lexy/input/string_input.hpp>

namespace
{
ast::document parse(const char* str)
{
    INFO(str);
    auto result
        = lexy::parse<grammar::document>(lexy::zstring_input<lexy::utf8_encoding>(str), lexy::noop);
    REQUIRE(result);
    return std::move(result).value();
}

void fail(const char* str)
{
    INFO(str);
    CHECK(!lexy::match<grammar::document>(lexy::zstring_input<lexy::utf8_encoding>(str)));
}
} // namespace

TEST_CASE("ini empty documents and comments")
{
    for (auto str : {"", " \t", "\n\r\n\n", ";", "# comment", "  # comment\r\n; other\n \t"})
    {
        auto doc = parse(str);
        CHECK(doc.globals.empty());
        CHECK(doc.sections.empty());
    }
}

TEST_CASE("ini globals and sections")
{
    auto doc = parse("; global settings\nname = demo\nempty=\n"
                     "\n[server]\nhost = localhost\nport = 8080\n"
                     "[empty]\n[client]\n# comment\nmode=fast");
    REQUIRE(doc.globals.size() == 2);
    CHECK(doc.globals[0].name == "name");
    CHECK(doc.globals[0].value == "demo");
    CHECK(doc.globals[1].value.empty());
    REQUIRE(doc.sections.size() == 3);
    CHECK(doc.sections[0].name == "server");
    REQUIRE(doc.sections[0].settings.size() == 2);
    CHECK(doc.sections[0].settings[1].name == "port");
    CHECK(doc.sections[0].settings[1].value == "8080");
    CHECK(doc.sections[1].name == "empty");
    CHECK(doc.sections[1].settings.empty());
    CHECK(doc.sections[2].settings[0].value == "fast");
}

TEST_CASE("ini names and whitespace")
{
    auto doc = parse(" \t[ section title ] \t\r\n"
                     " \tsetting name   =   value  \r\n"
                     "another  name\t=\tvalue\n"
                     "--key--=one\n``key``=two\nclose]bracket=three\n");
    REQUIRE(doc.sections.size() == 1);
    CHECK(doc.sections[0].name == " section title ");
    auto& settings = doc.sections[0].settings;
    REQUIRE(settings.size() == 5);
    CHECK(settings[0].name == "setting name");
    CHECK(settings[0].value == "value  ");
    CHECK(settings[1].name == "another  name");
    CHECK(settings[1].value == "value");
    CHECK(settings[2].name == "--key--");
    CHECK(settings[3].name == "``key``");
    CHECK(settings[4].name == "close]bracket");
}

TEST_CASE("ini values remain literal")
{
    auto doc = parse("hash = text # retained\nsemicolon = text ; retained\n"
                     "equals = a=b=c\nquoted = \"hello\"\npath = C:\\data\\file\n"
                     "empty=  \nspaces=one  two\n");
    REQUIRE(doc.globals.size() == 7);
    CHECK(doc.globals[0].value == "text # retained");
    CHECK(doc.globals[1].value == "text ; retained");
    CHECK(doc.globals[2].value == "a=b=c");
    CHECK(doc.globals[3].value == "\"hello\"");
    CHECK(doc.globals[4].value == "C:\\data\\file");
    CHECK(doc.globals[5].value.empty());
    CHECK(doc.globals[6].value == "one  two");
}

TEST_CASE("ini duplicates and case remain distinct")
{
    auto doc = parse("key=one\nkey=two\nKey=three\n[same]\nk=a\n[same]\nk=b\n");
    REQUIRE(doc.globals.size() == 3);
    CHECK(doc.globals[0].value == "one");
    CHECK(doc.globals[1].value == "two");
    CHECK(doc.globals[2].name == "Key");
    REQUIRE(doc.sections.size() == 2);
    CHECK(doc.sections[0].name == doc.sections[1].name);
    CHECK(doc.sections[0].settings[0].value == "a");
    CHECK(doc.sections[1].settings[0].value == "b");
}

TEST_CASE("ini Unicode and final lines")
{
    auto doc = parse("[配置]\r\n名称 = café 世界");
    REQUIRE(doc.sections.size() == 1);
    CHECK(doc.sections[0].name == "配置");
    REQUIRE(doc.sections[0].settings.size() == 1);
    CHECK(doc.sections[0].settings[0].name == "名称");
    CHECK(doc.sections[0].settings[0].value == "café 世界");
    CHECK(parse("[last]").sections.size() == 1);
    CHECK(parse("empty=").globals[0].value.empty());
    CHECK(parse("key=value\n# trailing comment").globals.size() == 1);
}

TEST_CASE("ini malformed lines")
{
    for (auto str : {"[]=value\n", "[]\n", "[unclosed\n", "[[nested]]\n", "[multi\nline]\n",
                     "[section] key=value\n", "[section] # comment\n", "missing_equals\n",
                     "=value\n", "name:value\n", "key\n=value\n", "bad\tname=value\n",
                     "bad#name=value\n", "bad;name=value\n", "bad[name=value\n",
                     "[s]\nkey\nother=value\n", "key=value\rother=value"})
        fail(str);
}

TEST_CASE("ini complete document")
{
    parse(R"sample(; Global settings
application = lexy example
optional =

[server]
host = localhost
port = 8080

[display]
window title = A small INI example
accent = #336699
message = semicolons ; and hashes # are literal in values

[search paths]
path = ./assets
path = ./shared
)sample");
}
