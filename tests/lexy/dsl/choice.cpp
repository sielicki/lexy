// Copyright (C) 2020-2025 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include <lexy/dsl/choice.hpp>

#include "verify.hpp"
#include <lexy/action/parse_as_tree.hpp>
#include <lexy/dsl/error.hpp>
#include <lexy/dsl/if.hpp>
#include <lexy/dsl/option.hpp>
#include <lexy/dsl/production.hpp>
#include <lexy/dsl/recover.hpp>
#include <lexy/dsl/whitespace.hpp>

namespace
{
template <int Id>
struct label
{
    static constexpr auto id   = Id;
    static constexpr auto name = "label";

    static constexpr auto rule = dsl::try_(LEXY_LIT("!"));
};

struct choice_whitespace
{
    static constexpr auto whitespace = dsl::lit_c<'.'>;
};

enum class choice_token_kind
{
    narrow,
    wide,
};
} // namespace

template <>
constexpr auto lexy::token_kind_map_for<choice_token_kind>
    = lexy::token_kind_map.map<choice_token_kind::narrow>(dsl::lit_c<'e'>)
          .map<choice_token_kind::wide>(dsl::lit_c<u'e'>);

TEST_CASE("dsl::operator|")
{
    constexpr auto callback = lexy::callback<int>([](const char*) { return 42; },
                                                  [](const char*, auto p) {
                                                      return std::remove_pointer_t<decltype(p)>::id;
                                                  });

    SUBCASE("simple")
    {
        constexpr auto rule
            = LEXY_LIT("abc") >> dsl::p<label<0>> | LEXY_LIT("def") >> dsl::p<label<1>>;
        CHECK(lexy::is_branch_rule<decltype(rule)>);

        auto empty = LEXY_VERIFY("");
        CHECK(empty.status == test_result::fatal_error);
        CHECK(empty.trace == test_trace().error(0, 0, "exhausted choice").cancel());

        auto abc = LEXY_VERIFY("abc!");
        CHECK(abc.status == test_result::success);
        CHECK(abc.value == 0);
        CHECK(abc.trace == test_trace().literal("abc").production("label").literal("!"));

        auto def = LEXY_VERIFY("def!");
        CHECK(def.status == test_result::success);
        CHECK(def.value == 1);
        CHECK(def.trace == test_trace().literal("def").production("label").literal("!"));

        auto branch_error = LEXY_VERIFY("abc");
        CHECK(branch_error.status == test_result::recovered_error);
        CHECK(branch_error.value == 0);
        CHECK(branch_error.trace
              == test_trace()
                     .literal("abc")
                     .production("label")
                     .expected_literal(3, "!", 0)
                     .recovery());
    }
    SUBCASE("branches are ordered")
    {
        constexpr auto rule
            = LEXY_LIT("a") >> dsl::p<label<0>> | LEXY_LIT("abc") >> dsl::p<label<1>>;
        CHECK(lexy::is_branch_rule<decltype(rule)>);

        auto empty = LEXY_VERIFY("");
        CHECK(empty.status == test_result::fatal_error);
        CHECK(empty.trace == test_trace().error(0, 0, "exhausted choice").cancel());

        auto a = LEXY_VERIFY("a!");
        CHECK(a.status == test_result::success);
        CHECK(a.value == 0);
        CHECK(a.trace == test_trace().literal("a").production("label").literal("!"));

        auto abc = LEXY_VERIFY("abc!");
        CHECK(abc.status == test_result::recovered_error);
        CHECK(abc.value == 0);
        CHECK(abc.trace
              == test_trace()
                     .literal("a")
                     .production("label")
                     .expected_literal(1, "!", 0)
                     .recovery());
    }
    SUBCASE("with else")
    {
        constexpr auto rule = LEXY_LIT("abc") >> dsl::p<label<0>>   //
                              | LEXY_LIT("def") >> dsl::p<label<1>> //
                              | dsl::else_ >> dsl::p<label<2>>;
        CHECK(lexy::is_rule<decltype(rule)>);

        auto empty = LEXY_VERIFY("");
        CHECK(empty.status == test_result::recovered_error);
        CHECK(empty.value == 2);
        CHECK(empty.trace
              == test_trace().production("label").expected_literal(0, "!", 0).recovery());

        auto abc = LEXY_VERIFY("abc!");
        CHECK(abc.status == test_result::success);
        CHECK(abc.value == 0);
        CHECK(abc.trace == test_trace().literal("abc").production("label").literal("!"));

        auto def = LEXY_VERIFY("def!");
        CHECK(def.status == test_result::success);
        CHECK(def.value == 1);
        CHECK(def.trace == test_trace().literal("def").production("label").literal("!"));

        auto branch_error = LEXY_VERIFY("abc");
        CHECK(branch_error.status == test_result::recovered_error);
        CHECK(branch_error.value == 0);
        CHECK(branch_error.trace
              == test_trace()
                     .literal("abc")
                     .production("label")
                     .expected_literal(3, "!", 0)
                     .recovery());
    }
    SUBCASE("with error")
    {
        struct my_error
        {
            static constexpr auto name()
            {
                return "my error";
            }
        };

        constexpr auto rule = LEXY_LIT("abc") >> dsl::p<label<0>>   //
                              | LEXY_LIT("def") >> dsl::p<label<1>> //
                              | dsl::error<my_error>;
        CHECK(lexy::is_rule<decltype(rule)>);

        auto empty = LEXY_VERIFY("");
        CHECK(empty.status == test_result::fatal_error);
        CHECK(empty.trace == test_trace().error(0, 0, "my error").cancel());

        auto abc = LEXY_VERIFY("abc!");
        CHECK(abc.status == test_result::success);
        CHECK(abc.value == 0);
        CHECK(abc.trace == test_trace().literal("abc").production("label").literal("!"));

        auto def = LEXY_VERIFY("def!");
        CHECK(def.status == test_result::success);
        CHECK(def.value == 1);
        CHECK(def.trace == test_trace().literal("def").production("label").literal("!"));

        auto branch_error = LEXY_VERIFY("abc");
        CHECK(branch_error.status == test_result::recovered_error);
        CHECK(branch_error.value == 0);
        CHECK(branch_error.trace
              == test_trace()
                     .literal("abc")
                     .production("label")
                     .expected_literal(3, "!", 0)
                     .recovery());
    }

    SUBCASE("as branch")
    {
        constexpr auto rule
            = dsl::if_(LEXY_LIT("abc") >> dsl::p<label<0>> | LEXY_LIT("def") >> dsl::p<label<1>>);
        CHECK(lexy::is_rule<decltype(rule)>);

        auto empty = LEXY_VERIFY("");
        CHECK(empty.status == test_result::success);
        CHECK(empty.value == 42);
        CHECK(empty.trace == test_trace());

        auto abc = LEXY_VERIFY("abc!");
        CHECK(abc.status == test_result::success);
        CHECK(abc.value == 0);
        CHECK(abc.trace == test_trace().literal("abc").production("label").literal("!"));

        auto def = LEXY_VERIFY("def!");
        CHECK(def.status == test_result::success);
        CHECK(def.value == 1);
        CHECK(def.trace == test_trace().literal("def").production("label").literal("!"));

        auto branch_error = LEXY_VERIFY("abc");
        CHECK(branch_error.status == test_result::recovered_error);
        CHECK(branch_error.value == 0);
        CHECK(branch_error.trace
              == test_trace()
                     .literal("abc")
                     .production("label")
                     .expected_literal(3, "!", 0)
                     .recovery());
    }
}

TEST_CASE("dsl::operator| - single code units")
{
    constexpr auto callback
        = lexy::callback<int>([](const auto*) { return 42; }, [](const auto*, auto) { return 0; });

    SUBCASE("optional branch")
    {
        constexpr auto choice = dsl::lit_c<'e'> | dsl::lit_c<'E'> | dsl::lit_c<u'e'>;
        constexpr auto rule   = dsl::if_(choice >> dsl::p<label<0>>);

        auto empty = LEXY_VERIFY("");
        CHECK(empty.status == test_result::success);
        CHECK(empty.value == 42);
        CHECK(empty.trace == test_trace());
        auto other = LEXY_VERIFY("x!");
        CHECK(other.status == test_result::success);
        CHECK(other.value == 42);
        CHECK(other.trace == test_trace());

        auto lower = LEXY_VERIFY("e!");
        CHECK(lower.status == test_result::success);
        CHECK(lower.value == 0);
        CHECK(lower.trace == test_trace().literal("e").production("label").literal("!"));
        auto upper = LEXY_VERIFY("E!");
        CHECK(upper.status == test_result::success);
        CHECK(upper.value == 0);
        CHECK(upper.trace == test_trace().literal("E").production("label").literal("!"));

        auto missing = LEXY_VERIFY("e");
        CHECK(missing.status == test_result::recovered_error);
        CHECK(missing.trace
              == test_trace()
                     .literal("e")
                     .production("label")
                     .expected_literal(1, "!", 0)
                     .recovery());
    }
    SUBCASE("whitespace")
    {
        constexpr auto rule = dsl::if_(dsl::lit_c<'e'> | dsl::lit_c<'E'>) + LEXY_LIT("!");
        struct production : test_production_for<decltype(rule)>, choice_whitespace
        {};
        auto result = LEXY_VERIFY_P(production, "E...!");
        CHECK(result.status == test_result::success);
        CHECK(result.trace == test_trace().literal("E").whitespace("...").literal("!"));
    }
    SUBCASE("wide code units")
    {
        constexpr auto rule  = dsl::if_(dsl::lit_c<u'\u00E4'> | dsl::lit_c<u'\u00F6'>);
        auto           first = LEXY_VERIFY(u"\u00E4");
        CHECK(first.status == test_result::success);
        CHECK(first.trace == test_trace().literal("\\u00E4"));
        auto second = LEXY_VERIFY(u"\u00F6");
        CHECK(second.status == test_result::success);
        CHECK(second.trace == test_trace().literal("\\u00F6"));
        auto empty = LEXY_VERIFY(u"");
        CHECK(empty.status == test_result::success);
        CHECK(empty.trace == test_trace());
    }
    SUBCASE("optional value")
    {
        constexpr auto rule  = dsl::opt(dsl::lit_c<'e'> | dsl::lit_c<'E'>);
        auto           empty = LEXY_VERIFY("");
        CHECK(empty.status == test_result::success);
        CHECK(empty.value == 0);
        auto match = LEXY_VERIFY("E");
        CHECK(match.status == test_result::success);
        CHECK(match.value == 42);
        CHECK(match.trace == test_trace().literal("E"));
    }
    SUBCASE("first matching token kind")
    {
        auto                                                     input = lexy::zstring_input("e");
        lexy::parse_tree_for<decltype(input), choice_token_kind> tree;

        constexpr auto narrow_first = dsl::if_(dsl::lit_c<'e'> | dsl::lit_c<u'e'>);
        auto           narrow
            = lexy::parse_as_tree<production_for<decltype(narrow_first)>>(tree, input, lexy::noop);
        REQUIRE(narrow);
        REQUIRE(tree.root().children().size() == 1);
        CHECK(tree.root().children().begin()->kind() == choice_token_kind::narrow);

        constexpr auto wide_first = dsl::if_(dsl::lit_c<u'e'> | dsl::lit_c<'e'>);
        auto           wide
            = lexy::parse_as_tree<production_for<decltype(wide_first)>>(tree, input, lexy::noop);
        REQUIRE(wide);
        REQUIRE(tree.root().children().size() == 1);
        CHECK(tree.root().children().begin()->kind() == choice_token_kind::wide);
    }
}
