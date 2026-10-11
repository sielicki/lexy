// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

// Parse chess PGN using recursive variations and line-sensitive comments.
// Supports tag pairs, SAN/LAN moves, results, annotations, nested comments, Unicode
// pieces and punctuation, HTML entities, null moves, and variant drops.
// Headerless movetext requires a result; a header needs movetext or a result.
// Percent comments start in column zero. Chess legality is not checked.
//
// Usage: build/examples/pgn <filename> (UTF-8 input).
// Playground: paste this file and select the entry production named production.
// Build target: lexy_example_pgn; test target: lexy_test_pgn.
// Reference: https://github.com/rolandwalker/tree-sitter-pgn/tree/v1.4.3
// Located through Nix: tree-sitter-grammars.tree-sitter-pgn.src
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

    // Generate numeric HTML references without enumerating hexadecimal case variants.
    template <unsigned Value, unsigned Base, char32_t... Digits>
    constexpr auto numeric_entity()
    {
        if constexpr (Value != 0)
            return numeric_entity<Value / Base, Base, U"0123456789abcdef"[Value % Base],
                                  Digits...>();
        else if constexpr (Base == 16)
            return dsl::ascii::case_folding(dsl::lit_cp<'&', '#', 'x', Digits..., ';'>);
        else
            return dsl::lit_cp<'&', '#', Digits..., ';'>;
    }
    template <char32_t... CodePoints, typename NamedEntities>
    constexpr auto glyphs(NamedEntities entities)
    {
        // Keep hexadecimal references in their own literal set: case folding must
        // not affect case-sensitive names such as TN, &Oslash;, and &oslash;.
        return dsl::token((dsl::literal_set(dsl::lit_cp<CodePoints>...) / entities)
                          | dsl::literal_set(numeric_entity<CodePoints, 10>()...)
                          | dsl::literal_set(numeric_entity<CodePoints, 16>()...));
    }

    constexpr auto o_symbol = LEXY_ASCII_ONE_OF("0Oo");
    constexpr auto slash_symbol
        = glyphs<U'/', U'⁄', U'∕', U'⟋', U'⧸'>(dsl::literal_set(LEXY_LIT("&sol;")));
    constexpr auto asterisk_symbol = glyphs<U'*', U'٭', U'⁎', U'∗', U'✱'>(
        dsl::literal_set(LEXY_LIT("&ast;")) / LEXY_LIT("&midast;") / LEXY_LIT("&lowast;"));
    constexpr auto plus_symbol = glyphs<U'+', U'᛭', U'➕'>(dsl::literal_set(LEXY_LIT("&plus;")));
    constexpr auto dash_symbol = glyphs<U'-', U'‐', U'‑', U'‒', U'–', U'—', U'−', U'➖'>(
        dsl::literal_set(LEXY_LIT("&hyphen;")) / LEXY_LIT("&dash;") / LEXY_LIT("&ndash;")
        / LEXY_LIT("&mdash;") / LEXY_LIT("&minus;"));
    constexpr auto half_symbol
        = glyphs<U'½'>(dsl::literal_set(LEXY_LIT("&frac12;")) / LEXY_LIT("&half;"));
    constexpr auto double_quote_symbol
        = glyphs<U'"', U'«', U'»', U'ʺ', U'ˮ', U'˝', U'˶', U'ײ', U'״', U'“', U'”', U'‟', U'″', U'‶',
                 U'〃', U'＂'>(dsl::literal_set(LEXY_LIT("&quot;")) / LEXY_LIT("&laquo;")
                               / LEXY_LIT("&raquo;") / LEXY_LIT("&ldquo;") / LEXY_LIT("&rdquo;"));
    constexpr auto infinity_symbol = glyphs<U'∞'>(dsl::literal_set(LEXY_LIT("&infin;")));
    constexpr auto equals_symbol
        = glyphs<U'=', U'⹀', U'＝'>(dsl::literal_set(LEXY_LIT("&equals;")));

    // Chess annotation glyphs: evaluations, positional features, and punctuation.
    constexpr auto annotation_symbol
        = glyphs<U'!', U'?', U'=', U'<', U'±', U'Ø', U'ø', U'‼', U'‽', U'⁇', U'⁈', U'⁉', U'↑', U'→',
                 U'⇆', U'⇈', U'⇔', U'⇗', U'∆', U'∇', U'∓', U'≤', U'⊕', U'⊙', U'⊞', U'⊥', U'⌓', U'└',
                 U'┘', U'□', U'○', U'◺', U'◿', U'☒', U'♂', U'✕', U'⟪', U'⟫', U'⟳', U'⨀', U'⨁', U'⩱',
                 U'⩲', U'⬒', U'⬓'>(
            dsl::literal_set(LEXY_LIT("!!")) / LEXY_LIT("!?") / LEXY_LIT("?!") / LEXY_LIT("??")
            / LEXY_LIT("==") / LEXY_LIT("<=") / LEXY_LIT("TN") / LEXY_LIT("RR") / LEXY_LIT("ep")
            / LEXY_LIT("e.p.") / LEXY_LIT("&excl;") / LEXY_LIT("&quest;") / LEXY_LIT("&equals;")
            / LEXY_LIT("&lt;") / LEXY_LIT("&le;") / LEXY_LIT("&leq;") / LEXY_LIT("&plusmn;")
            / LEXY_LIT("&pm;") / LEXY_LIT("&PlusMinus;") / LEXY_LIT("&mnplus;") / LEXY_LIT("&mp;")
            / LEXY_LIT("&MinusPlus;") / LEXY_LIT("&pluse;") / LEXY_LIT("&eplus;")
            / LEXY_LIT("&Oslash;") / LEXY_LIT("&oslash;") / LEXY_LIT("&squ;") / LEXY_LIT("&square;")
            / LEXY_LIT("&Square;") / LEXY_LIT("&cir;") / LEXY_LIT("&odot;") / LEXY_LIT("&xodot;")
            / LEXY_LIT("&bigodot;") / LEXY_LIT("&oplus;") / LEXY_LIT("&xoplus;")
            / LEXY_LIT("&bigoplus;") / LEXY_LIT("&uarr;") / LEXY_LIT("&uparrow;")
            / LEXY_LIT("&UpArrow;") / LEXY_LIT("&ShortUparrow;") / LEXY_LIT("&rarr;")
            / LEXY_LIT("&rightarrow;") / LEXY_LIT("&RightArrow;") / LEXY_LIT("&srarr;")
            / LEXY_LIT("&ShortRightArrow;") / LEXY_LIT("&lrarr;") / LEXY_LIT("&leftarrow;")
            / LEXY_LIT("&LeftArrow;") / LEXY_LIT("&slarr;") / LEXY_LIT("&ShortLeftArrow;")
            / LEXY_LIT("&harr;") / LEXY_LIT("&leftrightarrow;") / LEXY_LIT("&LeftRightArrow;")
            / LEXY_LIT("&nearr;") / LEXY_LIT("&nearrow;") / LEXY_LIT("&UpperRightArrow;")
            / LEXY_LIT("&nabla;") / LEXY_LIT("&Del;") / LEXY_LIT("&profsurf;") / LEXY_LIT("&plusb;")
            / LEXY_LIT("&boxplus;") / LEXY_LIT("&Lang;") / LEXY_LIT("&Rang;") / LEXY_LIT("&bottom;")
            / LEXY_LIT("&bot;") / LEXY_LIT("&perp;") / LEXY_LIT("&UpTee;") / LEXY_LIT("&male;")
            / LEXY_LIT("&boxur;") / LEXY_LIT("&boxul;") / LEXY_LIT("&lltri;"));
    struct rest_of_line_comment : lexy::token_production
    {
        static constexpr auto rule
            = dsl::lit_c<';'> >> dsl::while_(dsl::code_point - dsl::ascii::newline);
    };
    struct percent_comment : lexy::token_production
    {
        static constexpr auto rule = dsl::lit_c<'%'> >> dsl::until(dsl::newline).or_eof();
    };
    // Recognize '%' directly after LF/CRLF, before skipping any indentation.
    constexpr auto line_space
        = (dsl::newline >> dsl::while_(dsl::inline_<percent_comment>)) | dsl::unicode::space;
    struct command : lexy::token_production
    {
        static constexpr auto rule
            = LEXY_LIT("[%") >> dsl::while_one(dsl::ascii::alnum)
                                    + dsl::while_(dsl::code_point - LEXY_ASCII_ONE_OF("{}]"))
                                    + dsl::lit_c<']'>;
    };
    struct inline_comment : lexy::token_production
    {
        static constexpr auto rule
            = dsl::lit_c<'{'> >> dsl::while_(dsl::p<command>
                                             | (dsl::peek(dsl::lit_c<'{'>)
                                                >> dsl::recurse<inline_comment>)
                                             | (dsl::code_point - LEXY_ASCII_ONE_OF("{}")))
                                     + dsl::lit_c<'}'>;
    };
    struct section_comment : lexy::token_production
    {
        static constexpr auto rule
            = dsl::token(dsl::while_one(dsl::code_point - dsl::ascii::newline) + dsl::newline
                         + dsl::times<4>(dsl::lit_c<'-'>) + dsl::while_(dsl::lit_c<'-'>)
                         + dsl::peek(dsl::newline));
    };
    struct tagpair_key
    {
        static constexpr auto rule
            = dsl::token((dsl::code_point - dsl::unicode::space - LEXY_ASCII_ONE_OF("[%"))
                         + dsl::while_(dsl::code_point - dsl::unicode::space));
    };
    struct tagpair_value : lexy::token_production
    {
        // Embedded quotes, escaped or not, occur in real game collections. A closing
        // quote is recognized by the following tag delimiter, rather than by '\'.
        static constexpr auto rule
            = double_quote_symbol
              >> dsl::while_(dsl::peek_not(double_quote_symbol + dsl::while_(dsl::unicode::space)
                                           + dsl::lit_c<']'>)
                             >> (dsl::code_point - dsl::ascii::newline))
                     + double_quote_symbol;
    };
    struct tagpair
    {
        static constexpr auto rule
            = dsl::lit_c<'['> >> dsl::while_(dsl::lit_c<'['>) + dsl::p<tagpair_key>
                                     + dsl::p<tagpair_value> + dsl::while_one(dsl::lit_c<']'>);
    };
    struct header
    {
        static constexpr auto rule
            = dsl::p<tagpair> >> dsl::while_(dsl::p<tagpair> | dsl::p<rest_of_line_comment>);
    };
    constexpr auto piece  = LEXY_ASCII_ONE_OF("RNBQK") / dsl::lit_cp<U'♖'> / dsl::lit_cp<U'♘'>
                            / dsl::lit_cp<U'♗'> / dsl::lit_cp<U'♕'> / dsl::lit_cp<U'♔'>
                            / dsl::lit_cp<U'♜'> / dsl::lit_cp<U'♞'> / dsl::lit_cp<U'♝'>
                            / dsl::lit_cp<U'♛'> / dsl::lit_cp<U'♚'>;
    constexpr auto file   = LEXY_ASCII_ONE_OF("abcdefgh");
    constexpr auto rank   = LEXY_ASCII_ONE_OF("12345678");
    constexpr auto square = dsl::token(file + rank);
    constexpr auto capture_symbol = LEXY_ASCII_ONE_OF("xX:");
    constexpr auto promotion      = dsl::if_(equals_symbol) + piece;
    constexpr auto pawn_target    = dsl::token(file + LEXY_ASCII_ONE_OF("18") + promotion)
                                    | dsl::token(file + LEXY_ASCII_ONE_OF("234567"));
    constexpr auto check          = (plus_symbol >> dsl::if_(plus_symbol)) | dsl::lit_c<'#'>;
    struct san_move : lexy::token_production
    {
        static constexpr auto rule = [] {
            auto castle
                = dsl::token(o_symbol + dash_symbol + o_symbol + dsl::if_(dash_symbol >> o_symbol));
            auto null_move      = dsl::token(LEXY_ASCII_ONE_OF("Zz") + o_symbol)
                                  | dsl::token(dash_symbol + dash_symbol)
                                  | dsl::ascii::case_folding(LEXY_LIT("pass"));
            auto disambiguation = dsl::token(square + dsl::if_(capture_symbol) + square)
                                  | dsl::token(file + dsl::if_(capture_symbol) + square)
                                  | dsl::token(rank + dsl::if_(capture_symbol) + square)
                                  | dsl::token(dsl::if_(capture_symbol) + square);
            auto major
                = dsl::token(piece + (dsl::token(dsl::lit_c<'@'> + square) | disambiguation));
            auto pawn = dsl::token(square + dsl::if_(capture_symbol) + pawn_target)
                        | dsl::token(file + dsl::if_(capture_symbol) + pawn_target) | pawn_target;
            auto drop = dsl::token(dsl::if_(dsl::lit_c<'P'> / dsl::lit_cp<U'♙'> / dsl::lit_cp<U'♟'>)
                                   + dsl::lit_c<'@'> + square);
            return (castle | null_move | major | drop | pawn) >> dsl::if_(check);
        }();
    };
    struct lan_move : lexy::token_production
    {
        static constexpr auto rule = dsl::token(square + (dash_symbol | capture_symbol) + square
                                                + dsl::if_(dsl::token(promotion)))
                                     >> dsl::if_(check);
    };
    struct move_number : lexy::token_production
    {
        static constexpr auto rule = [] {
            auto dot = dsl::lit_c<'.'> / dsl::lit_cp<U'𝅭'> / dsl::lit_cp<U'․'> / dsl::lit_cp<U'܁'>
                       / dsl::lit_cp<U'܂'> / dsl::lit_cp<U'꘎'> / dsl::lit_cp<U'٠'>
                       / dsl::lit_cp<U'۰'> / dsl::lit_cp<U'ꓸ'> / dsl::lit_cp<0x200E>;
            return dsl::token(dsl::digits<> + dsl::if_(LEXY_ASCII_ONE_OF("AaBb"))
                              + dsl::while_one(line_space | dot));
        }();
    };
    struct result_code : lexy::token_production
    {
        static constexpr auto rule = [] {
            auto space = dsl::if_(dsl::unicode::space);
            auto half = dsl::token(dsl::lit_c<'1'> + space + slash_symbol + space + dsl::lit_c<'2'>)
                        | half_symbol;
            return dsl::token(half + space + dash_symbol + space + half)
                   | dsl::token(dsl::lit_c<'1'> + space + dash_symbol + space + o_symbol)
                   | dsl::token(o_symbol + space + dash_symbol + space + dsl::lit_c<'1'>)
                   | asterisk_symbol;
        }();
    };
    struct annotation : lexy::token_production
    {
        static constexpr auto rule = [] {
            auto balance
                = dsl::token(plus_symbol + dsl::if_(slash_symbol) + dash_symbol)
                  | dsl::token(dash_symbol + dsl::if_(slash_symbol) + plus_symbol)
                  | dsl::token(equals_symbol + slash_symbol + infinity_symbol)
                  | dsl::token(infinity_symbol + dsl::while_(dsl::unicode::space) + slash_symbol
                               + dsl::while_(dsl::unicode::space) + equals_symbol)
                  | dsl::token(slash_symbol + equals_symbol)
                  | dsl::token(equals_symbol + plus_symbol)
                  | dsl::token(plus_symbol + equals_symbol);
            return (dsl::lit_c<'$'> >> dsl::digits<>) | balance
                   | (dsl::followed_by(dsl::lit_c<'N'>,
                                       dsl::unicode::space / LEXY_ASCII_ONE_OF("){(")))
                   | annotation_symbol | equals_symbol | dash_symbol | infinity_symbol;
        }();
    };
    struct variation;
    struct element
    {
        static constexpr auto rule
            = dsl::p<inline_comment> | dsl::p<rest_of_line_comment>
              | (dsl::peek(dsl::lit_c<'('>) >> dsl::recurse<variation>)
              | (dsl::peek(square + dash_symbol) >> dsl::p<lan_move>) | dsl::p<san_move>
              | dsl::p<lan_move> | dsl::p<move_number> | dsl::p<annotation>;
    };
    struct variation
    {
        static constexpr auto rule
            = dsl::lit_c<'('> + dsl::while_(dsl::p<element>) + dsl::lit_c<')'>;
    };
    struct game
    {
        using scan_result          = lexy::scan_result<void>;
        static constexpr auto rule = dsl::scan;
        template <typename Context, typename Reader>
        static scan_result scan(lexy::rule_scanner<Context, Reader>& scanner)
        {
            bool has_header = scanner.branch(dsl::p<header>);
            bool moves      = false;
            while (scanner && !scanner.peek(dsl::p<result_code>) && scanner.branch(dsl::p<element>))
                moves = true;
            // Tolerate the source grammar's stray closing punctuation before a result.
            bool garbage = scanner.branch(dsl::while_one(dsl::lit_c<'}'> / dsl::lit_c<')'>));
            bool result  = scanner.branch(dsl::p<result_code>);
            if ((!has_header && !moves) || (!result && (garbage || !has_header || !moves)))
            {
                scanner.fatal_error("expected a game result", scanner.position());
                return lexy::scan_failed;
            }
            return bool(scanner);
        }
    };
    struct games
    {
        static constexpr auto whitespace = line_space;
        static constexpr auto rule
            = dsl::while_(dsl::p<rest_of_line_comment> | dsl::p<inline_comment>
                          | dsl::p<section_comment> | (dsl::peek_not(dsl::eof) >> dsl::p<game>))
              + dsl::eof;
    };
    struct document
    {
        // No initial whitespace skipping: '%' is also allowed at the input beginning.
        static constexpr auto rule = dsl::while_(dsl::p<percent_comment>) + dsl::p<games>;
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
