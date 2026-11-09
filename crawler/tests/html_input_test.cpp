#include "hfs/html_input.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {
using enum hfs::HtmlInputStatus;
}

TEST(HtmlInput, AcceptsUtf8AliasesQuotedParametersAndUndeclaredUtf8)
{
    for (const auto* type : {"text/html", "Text/HTML; CHARSET=Utf-8", "text/html; charset=utf8",
                             "text/html; charset=unicode-1-1-utf-8", " text/html ; charset=\"utf-8\"\t",
                             "text/html; note=\"x;charset=windows-1252\"; charset=\"utf\\-8\"",
                             "text/html;; note=\"\";"}) {
        SCOPED_TRACE(type);
        EXPECT_EQ(hfs::check_html_input(type, "<p>Caf\xC3\xA9 \xF0\x9F\x8C\x8D</p>"), ready);
    }
    EXPECT_EQ(hfs::check_html_input("text/html", {}), ready);
}

TEST(HtmlInput, RejectsOtherMediaAndMalformedOrAmbiguousParameters)
{
    for (const auto* type : {"text/plain", "text/htmlish", "application/xhtml+xml"}) {
        EXPECT_EQ(hfs::check_html_input(type, "<p>HTML-looking</p>"), not_html);
    }
    for (const auto* type : {"", "text/html junk", "text/html; charset", "text/html; charset=",
                             "text/html; charset=\"\"", "text/html; charset=\"utf-8",
                             "text/html; charset =utf-8", "text/html; charset= utf-8",
                             "text/html; charset=utf-8; charset=utf-8",
                             "text/html; note=\"bad\rvalue\"", "text/html; note=\"x\"junk"}) {
        SCOPED_TRACE(type);
        EXPECT_EQ(hfs::check_html_input(type, "<p>Text</p>"), invalid_content_type);
    }
}

TEST(HtmlInput, RejectsUnsupportedAndUnknownHttpCharsetsEvenForAsciiBodies)
{
    for (const auto* charset : {"windows-1252", "iso-8859-1", "utf-16", "shift_jis", "unknown", "us-ascii"}) {
        SCOPED_TRACE(charset);
        EXPECT_EQ(hfs::check_html_input(std::string{"text/html; charset="} + charset,
                                        "<meta charset=utf-8><p>ASCII</p>"), unsupported_encoding);
    }
}

TEST(HtmlInput, UsesBomBeforeHttpAndHttpBeforeMeta)
{
    EXPECT_EQ(hfs::check_html_input("text/html; charset=windows-1252",
                                    "\xEF\xBB\xBF<meta charset=windows-1252><p>UTF-8</p>"), ready);
    EXPECT_EQ(hfs::check_html_input("text/html; charset=utf-8",
                                    "<meta charset=windows-1252><p>UTF-8</p>"), ready);
    for (const auto& bom : {std::string{"\xFE\xFF", 2}, std::string{"\xFF\xFE", 2},
                            std::string{"\0\0\xFE\xFF", 4}, std::string{"\xFF\xFE\0\0", 4}}) {
        EXPECT_EQ(hfs::check_html_input("text/html; charset=utf-8", bom + "<p>Text</p>"),
                  unsupported_encoding);
    }
}

TEST(HtmlInput, ReadsEarlyMetaDeclarationsWithLexborWhenHttpCharsetIsAbsent)
{
    EXPECT_EQ(hfs::check_html_input("text/html", "<META CHARSET='utf-8'><p>Text</p>"), ready);
    EXPECT_EQ(hfs::check_html_input("text/html", "<meta charset=windows-1252><p>Text</p>"),
              unsupported_encoding);
    EXPECT_EQ(hfs::check_html_input("text/html",
        "<meta http-equiv='Content-Type' content='text/html; charset=shift_jis'>"), unsupported_encoding);
    EXPECT_EQ(hfs::check_html_input("text/html",
        "<!-- <meta charset=windows-1252> --><meta charset=utf-8>"), ready);
    EXPECT_EQ(hfs::check_html_input("text/html", "<meta charset=unknown><meta charset=windows-1252>"),
              unsupported_encoding);
    // HTML prescan rules map a UTF-16 meta label to UTF-8 for an ASCII-compatible stream.
    EXPECT_EQ(hfs::check_html_input("text/html", "<meta charset=utf-16><p>Text</p>"), ready);
    EXPECT_EQ(hfs::check_html_input("text/html",
        std::string(1024, ' ') + "<meta charset=windows-1252>"), ready);
}

TEST(HtmlInput, RejectsMalformedUtf8AndNulBytesInsteadOfRepairingThem)
{
    for (const auto& bytes : {std::string{"\x80"}, std::string{"\xC0\xAF"},
                              std::string{"\xC2"}, std::string{"\xC2 "},
                              std::string{"\xE0\x80\x80"}, std::string{"\xED\xA0\x80"},
                              std::string{"\xF0\x80\x80\x80"}, std::string{"\xF4\x90\x80\x80"},
                              std::string{"\xF5\x80\x80\x80"}, std::string{"\xFF"},
                              std::string{"\xE2\x82"}, std::string{"\xF0\x9F\x8C"},
                              std::string{"\0", 1}}) {
        EXPECT_EQ(hfs::check_html_input("text/html; charset=utf-8", bytes), invalid_text);
        EXPECT_EQ(hfs::check_html_input("text/html", std::string(1100, 'x') + bytes), invalid_text);
    }
    EXPECT_EQ(hfs::check_html_input("text/html", "\xEF\xBB\xBF\xFF"), invalid_text);
}

TEST(HtmlInput, AcceptsUnicodeScalarBoundariesAndAnExistingReplacementCharacter)
{
    EXPECT_EQ(hfs::check_html_input("text/html",
        "\x01\x7F\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80"
        "\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF\xEF\xBF\xBD"), ready);
}
