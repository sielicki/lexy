// Copyright (C) 2026 Jonathan Müller and lexy contributors
// SPDX-License-Identifier: BSL-1.0

#include "../../examples/diff.cpp" // NOLINT

#include <doctest/doctest.h>
#include <lexy/action/match.hpp>
#include <lexy/callback.hpp>
#include <lexy/input/string_input.hpp>
#include <string>

namespace
{
using tree_type = lexy::parse_tree_for<lexy::string_input<lexy::utf8_encoding>>;

tree_type parse(const char* str)
{
    INFO(str);
    auto      input = lexy::zstring_input<lexy::utf8_encoding>(str);
    tree_type tree;
    REQUIRE(lexy::parse_as_tree<grammar::source>(tree, input, lexy::noop));
    std::string restored;
    for (auto [event, node] : tree.traverse())
        if (event == lexy::traverse_event::leaf)
            restored.append(node.lexeme().begin(), node.lexeme().end());
    CHECK(restored == str);
    CHECK(tree.remaining_input().empty());
    return tree;
}

template <typename Production>
std::size_t count(const tree_type& tree)
{
    auto result = std::size_t(0);
    for (auto [event, node] : tree.traverse())
        if (event == lexy::traverse_event::enter && node.kind() == Production{})
            ++result;
    return result;
}

template <typename Production>
void line(const char* str)
{
    auto tree = parse(str);
    CHECK(count<Production>(tree) == 1);
}
} // namespace

TEST_CASE("diff text blocks and ambiguous triple markers")
{
    auto tree = parse("diff --git a/one b/one\n"
                      "index 1234..abcdef 100644\n"
                      "--- a/one\n+++ b/one\n"
                      "@@ -1,2 +1,2 @@ function\n"
                      "--- removed content\n+++ added content\n context\n"
                      "@@ -20 +20 @@\n-old\n+new\n\\ No newline at end of file\n"
                      "diff --git a/two b/two\nnew file mode 100644\n"
                      "--- /dev/null\n+++ b/two\n@@ -0,0 +1 @@\n+hello");
    CHECK(count<grammar::block>(tree) == 2);
    CHECK(count<grammar::old_file>(tree) == 2);
    CHECK(count<grammar::new_file>(tree) == 2);
    CHECK(count<grammar::hunk>(tree) == 3);
    CHECK(count<grammar::addition>(tree) == 3);
    CHECK(count<grammar::deletion>(tree) == 2);
    CHECK(count<grammar::context>(tree) == 1);
    CHECK(count<grammar::special>(tree) == 1);
    CHECK(count<grammar::unrecognized>(tree) == 0);
}

TEST_CASE("diff standalone lines")
{
    line<grammar::old_file>("--- /dev/null");
    line<grammar::new_file>("+++ b/space in name\t2026-10-10\n");
    line<grammar::location>("@@ -0,0 +1 @@\n");
    line<grammar::addition>("++++not a header\n");
    line<grammar::addition>("> normal diff\n");
    line<grammar::deletion>("---\n");
    line<grammar::deletion>("----not a header\n");
    line<grammar::deletion>("< normal diff\n");
    line<grammar::change>("! changed\n");
    line<grammar::context>(" # this is context\n");
    line<grammar::comment>("# comment\n");
    line<grammar::special>("\\ No newline at end of file");
    line<grammar::unrecognized>("2c2\n");
    line<grammar::unrecognized>("***************\n");
    line<grammar::unrecognized>("diff --git a/x b/x"); // A block needs a newline.
    parse("");
    parse("\n\r\n\n");
}

TEST_CASE("diff metadata")
{
    line<grammar::file_change>("new file mode 100644\n");
    line<grammar::file_change>("deleted file mode 100755\n");
    line<grammar::file_change>("old mode 100644\n");
    line<grammar::file_change>("new mode 100755\n");
    line<grammar::file_change>("rename from old name.txt\n");
    line<grammar::file_change>("rename to new name.txt\n");
    line<grammar::file_change>("copy from source.txt\n");
    line<grammar::file_change>("copy to destination.txt\n");
    line<grammar::similarity>("similarity index 79%\n");
    line<grammar::dissimilarity>("dissimilarity index 100%\n");
    line<grammar::index>("index 1234..abcd\n");
    line<grammar::index>("index 1234..abcd  \n");
    line<grammar::index>("index 0000000..abcdef0 100644\n");
    line<grammar::binary_change>("Binary files a/image.png and b/image.png differ\n");
    auto sha256
        = std::string("index ") + std::string(64, 'a') + ".." + std::string(64, 'b') + " 100644\n";
    line<grammar::index>(sha256.c_str());
}

TEST_CASE("diff malformed headers fall back without losing input")
{
    line<grammar::unrecognized>("index abc..1234\n");
    line<grammar::unrecognized>("index ABCD..1234\n");
    line<grammar::unrecognized>("index 1234..5678 rubbish\n");
    auto oversized = std::string("index ") + std::string(65, 'a') + "..abcd\n";
    line<grammar::unrecognized>(oversized.c_str());
    line<grammar::unrecognized>("@@ -1 + @@\n");
    line<grammar::unrecognized>("new file mode unknown\n");
    line<grammar::unrecognized>("similarity index 90\n");
    line<grammar::unrecognized>("Binary files a and b\n");
    auto tree = parse("diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1 +1 @@\n"
                      "? unrecognized hunk line\n+recognized\n");
    CHECK(count<grammar::hunk>(tree) == 1);
    CHECK(count<grammar::unrecognized>(tree) == 1);
    CHECK(count<grammar::addition>(tree) == 1);
}

TEST_CASE("diff binary patches and following text block")
{
    auto tree = parse("diff --git a/image b/image\nGIT binary patch\n"
                      "literal 4\nA00000\nBabcde\n\n"
                      "delta 3\nC!#$%&\n\n"
                      "diff --git a/text b/text\n--- a/text\n+++ b/text\n"
                      "@@ -1 +1 @@\n-before\n+after\n");
    CHECK(count<grammar::block>(tree) == 2);
    CHECK(count<grammar::binary_patch>(tree) == 1);
    CHECK(count<grammar::binary_hunk>(tree) == 2);
    CHECK(count<grammar::payload>(tree) == 2);
    CHECK(count<grammar::hunk>(tree) == 1);
    CHECK(count<grammar::addition>(tree) == 1);

    auto forward = parse("diff --git a/x b/x\nGIT binary patch\nliteral 0\nA00000\n");
    CHECK(count<grammar::binary_hunk>(forward) == 1);
    for (auto malformed : {"diff --git a/x b/x\nGIT binary patch\nliteral 4\n",
                           "diff --git a/x b/x\nGIT binary patch\nliteral 4\nA/0000\n",
                           "diff --git a/x b/x\nGIT binary patch\nliteral 4\nA\n",
                           "diff --git a/x b/x\nGIT binary patch\nliteral 4\nA00000"})
    {
        auto fallback = parse(malformed);
        CHECK(count<grammar::binary_patch>(fallback) == 0);
        CHECK(count<grammar::unrecognized>(fallback) >= 2);
    }
}

TEST_CASE("diff line endings, Unicode, and line boundaries")
{
    auto tree = parse("diff --git a/café b/café\r\n--- a/café\r\n+++ b/café\r\n"
                      "@@ -1 +1 @@\r\n-旧\r\n+新\r\n");
    CHECK(count<grammar::block>(tree) == 1);
    CHECK(count<grammar::addition>(tree) == 1);
    CHECK(count<grammar::deletion>(tree) == 1);
    line<grammar::unrecognized>("index 1234..\n");
    line<grammar::context>(" diff --git a/x b/x\n");
    for (auto invalid : {"text\rtext", "+\xC3\n"})
        CHECK(!lexy::match<grammar::source>(lexy::zstring_input<lexy::utf8_encoding>(invalid)));
}

TEST_CASE("diff complete document")
{
    parse(R"sample(diff --git a/greeting.cpp b/greeting.cpp
index 1234abcd..abcd1234 100644
--- a/greeting.cpp
+++ b/greeting.cpp
@@ -1,3 +1,3 @@
 int main() {
-    puts("Hello");
+    puts("Hello, lexy!");
 }
diff --git a/old-name.txt b/new-name.txt
similarity index 100%
rename from old-name.txt
rename to new-name.txt
)sample");
}
