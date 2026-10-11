// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Recognize diff output using line-sensitive whitespace and lookahead. Triple
// markers are file headers outside hunks and changed text inside them. Git metadata
// and binary patches receive structured nodes; unknown lines remain in the tree.
// Hunk counts and patch applicability are not checked.
//
// Usage: build/examples/diff <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_diff; test target: lexy_test_diff.
// Reference: https://github.com/tree-sitter-grammars/tree-sitter-diff/tree/v0.2.0
// Located through Nix: tree-sitter-grammars.tree-sitter-diff.src
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

    // No automatic whitespace skipping: the first space of a context line is significant.
    constexpr auto character = dsl::code_point - dsl::ascii::newline;
    constexpr auto tail      = dsl::token(dsl::while_(character));
    constexpr auto blank     = dsl::ascii::blank / dsl::ascii::other_space;
    constexpr auto ws        = dsl::while_(blank);
    constexpr auto gap       = dsl::while_one(blank);

    // Check the complete line before committing to a structured interpretation. A malformed
    // header can then fall back to unrecognized text, as in the highlighting grammar.
    template <typename Rule>
    constexpr auto line(Rule rule)
    {
        return dsl::peek(rule + ws + dsl::eol) >> rule + ws;
    }

    struct filename
    {
        // Keep paths, quotes, spaces, and timestamps verbatim; do not interpret Git quoting.
        static constexpr auto rule = dsl::token((dsl::code_point - dsl::ascii::space) + tail);
    };
    struct argument
    {
        static constexpr auto rule = dsl::while_one(dsl::ascii::word / dsl::lit_c<'-'>);
    };
    struct mode
    {
        static constexpr auto rule = dsl::digits<>;
    };
    struct score
    {
        static constexpr auto rule = dsl::digits<>;
    };
    struct size
    {
        static constexpr auto rule = dsl::digits<>;
    };
    struct commit
    {
        // Git object IDs can be abbreviated, SHA-1, or SHA-256: 4 through 64 lowercase hex digits.
        static constexpr auto rule = [] {
            auto hex = dsl::digit<dsl::hex_lower>;
            return dsl::token(dsl::peek_not(dsl::times<65>(hex)) + dsl::times<4>(hex)
                              + dsl::while_(hex));
        }();
    };
    struct linerange
    {
        static constexpr auto rule = (dsl::lit_c<'-'> | dsl::lit_c<'+'>)+dsl::digits<>
                                     + dsl::if_(dsl::comma >> dsl::digits<>);
    };

    struct command
    {
        static constexpr auto rule
            = line(LEXY_LIT("diff") + gap + dsl::p<argument> + gap + dsl::p<filename>);
    };
    struct file_change
    {
        static constexpr auto rule
            = line((LEXY_LIT("new") | LEXY_LIT("deleted")) + gap + LEXY_LIT("file") + gap
                   + LEXY_LIT("mode") + gap + dsl::p<mode>)
              | line((LEXY_LIT("new") | LEXY_LIT("old")) + gap + LEXY_LIT("mode") + gap
                     + dsl::p<mode>)
              | line((LEXY_LIT("rename") | LEXY_LIT("copy")) + gap
                     + (LEXY_LIT("from") | LEXY_LIT("to")) + gap + dsl::p<filename>);
    };
    struct binary_old_file
    {
        static constexpr auto name = "filename";
        static constexpr auto rule
            = dsl::token(dsl::while_one(dsl::peek_not(LEXY_LIT(" and ")) >> character));
    };
    struct binary_new_file
    {
        static constexpr auto name = "filename";
        static constexpr auto rule = dsl::token(
            dsl::while_one(dsl::peek_not(LEXY_LIT(" differ") + ws + dsl::eol) >> character));
    };
    struct binary_change
    {
        static constexpr auto rule
            = line(LEXY_LIT("Binary files ") + dsl::p<binary_old_file>
                   + LEXY_LIT(" and ") + dsl::p<binary_new_file> + LEXY_LIT(" differ"));
    };
    struct index
    {
        static constexpr auto rule
            = line(LEXY_LIT("index") + gap + dsl::p<commit> + LEXY_LIT("..") + dsl::p<commit>
                   + dsl::if_(dsl::peek(gap + dsl::ascii::digit) >> gap + dsl::p<mode>));
    };
    struct similarity
    {
        static constexpr auto rule = line(LEXY_LIT("similarity") + gap + LEXY_LIT("index") + gap
                                          + dsl::p<score> + dsl::lit_c<'%'>);
    };
    struct dissimilarity
    {
        static constexpr auto rule = line(LEXY_LIT("dissimilarity") + gap + LEXY_LIT("index") + gap
                                          + dsl::p<score> + dsl::lit_c<'%'>);
    };
    struct old_file
    {
        static constexpr auto rule
            = line(LEXY_LIT("---") + dsl::peek_not(dsl::lit_c<'-'>) + ws + dsl::p<filename>);
    };
    struct new_file
    {
        static constexpr auto rule
            = line(LEXY_LIT("+++") + dsl::peek_not(dsl::lit_c<'+'>) + ws + dsl::p<filename>);
    };
    struct location
    {
        static constexpr auto rule = line(LEXY_LIT("@@") + ws + dsl::p<linerange>
                                          + ws + dsl::p<linerange> + ws + LEXY_LIT("@@") + tail);
    };

    struct addition
    {
        static constexpr auto rule = (dsl::lit_c<'+'> | dsl::lit_c<'>'>) >> tail;
    };
    struct deletion
    {
        static constexpr auto rule = (dsl::lit_c<'-'> | dsl::lit_c<'<'>) >> tail;
    };
    struct change
    {
        static constexpr auto rule = dsl::lit_c<'!'> >> tail;
    };
    struct context
    {
        static constexpr auto rule = dsl::lit_c<' '> >> tail;
    };
    struct comment
    {
        static constexpr auto rule = dsl::lit_c<'#'> >> tail;
    };
    struct special
    {
        static constexpr auto rule = dsl::lit_c<'\\'> >> tail;
    };
    struct unrecognized
    {
        static constexpr auto rule = dsl::token(dsl::while_one(character));
    };

    struct metadata : lexy::transparent_production
    {
        static constexpr auto rule = dsl::p<file_change> | dsl::p<binary_change> | dsl::p<index>
                                     | dsl::p<similarity> | dsl::p<dissimilarity>;
    };
    struct changes
    {
        static constexpr auto rule = [] {
            // A valid command or location starts the next block/hunk. In this context, triple
            // markers are changed text, not old_file/new_file headers. Unknown lines survive.
            auto boundary = dsl::p<command> | dsl::p<location> | dsl::newline | dsl::eof;
            auto content  = dsl::p<addition> | dsl::p<deletion> | dsl::p<change> | dsl::p<context>
                            | dsl::p<special> | dsl::p<unrecognized>;
            return dsl::list(dsl::peek_not(boundary)
                             >> content + dsl::eol + dsl::while_(dsl::newline));
        }();
    };
    struct hunk
    {
        static constexpr auto rule = dsl::p<location> >> dsl::eol + dsl::if_(dsl::p<changes>);
    };
    struct hunks
    {
        static constexpr auto rule = dsl::list(dsl::p<hunk>);
    };

    struct payload
    {
        static constexpr auto rule = [] {
            auto base85 = dsl::ascii::alnum / LEXY_ASCII_ONE_OF("!#$%&()*+-;<=>?@^_`{|}~");
            auto row    = dsl::ascii::alpha + dsl::while_one(base85);
            return dsl::token(
                row + dsl::while_(dsl::peek(dsl::newline + row + dsl::eol) >> dsl::newline + row));
        }();
    };
    struct binary_hunk
    {
        static constexpr auto content = (LEXY_LIT("literal") | LEXY_LIT("delta")) + gap
                                        + dsl::p<size> + ws + dsl::newline + dsl::p<payload>
                                        + dsl::newline + dsl::while_(dsl::newline);
        static constexpr auto rule    = dsl::peek(content) >> content;
    };
    struct binary_patch
    {
        static constexpr auto content = LEXY_LIT("GIT binary patch") + ws + dsl::newline
                                        + dsl::p<binary_hunk> + dsl::if_(dsl::p<binary_hunk>);
        static constexpr auto rule    = dsl::peek(content) >> content;
    };

    struct block
    {
        static constexpr auto rule = [] {
            auto headers    = dsl::p<old_file> + dsl::newline + dsl::p<new_file> + dsl::newline;
            auto text_patch = dsl::peek(headers + dsl::p<location>) >> headers + dsl::p<hunks>;
            return dsl::peek(dsl::p<command> + dsl::newline)
                   >> dsl::p<command> + dsl::newline + dsl::while_(dsl::p<metadata> >> dsl::eol)
                          + dsl::if_(text_patch | dsl::p<binary_patch>);
        }();
    };
    struct source
    {
        static constexpr auto rule = [] {
            auto standalone = dsl::p<metadata> | dsl::p<old_file> | dsl::p<new_file>
                              | dsl::p<location> | dsl::p<addition> | dsl::p<deletion>
                              | dsl::p<change> | dsl::p<context> | dsl::p<comment> | dsl::p<special>
                              | dsl::p<unrecognized>;
            return dsl::while_(dsl::p<block> | dsl::newline | (standalone >> dsl::eol)) + dsl::eof;
        }();
    };
} // namespace grammar
} // namespace

using production = grammar::source;

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
    if (!lexy::parse_as_tree<grammar::source>(tree, file.buffer(),
                                              lexy_ext::report_error.path(argv[1])))
        return 2;
    lexy::visualize(stdout, tree);
}
#endif
