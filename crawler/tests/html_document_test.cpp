#include "hfs/html_document.hpp"

#include <gtest/gtest.h>
#include <string>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<hfs::HtmlDocument>);
static_assert(!std::is_copy_assignable_v<hfs::HtmlDocument>);
static_assert(std::is_nothrow_move_constructible_v<hfs::HtmlDocument>);
static_assert(std::is_nothrow_move_assignable_v<hfs::HtmlDocument>);

TEST(HtmlDocument, EmptyInputHasNoTitle)
{
    EXPECT_TRUE(hfs::HtmlDocument{std::string_view{}}.title().empty());
}

TEST(HtmlDocument, MissingTitleIsEmpty)
{
    EXPECT_TRUE(hfs::HtmlDocument{"<p>No title"}.title().empty());
}

TEST(HtmlDocument, EmptyTitleIsEmpty)
{
    EXPECT_TRUE(hfs::HtmlDocument{"<title></title>"}.title().empty());
}

TEST(HtmlDocument, NormalizesTitleWhitespaceAndDecodesEntities)
{
    // Omitted paragraph closing tags must not prevent title extraction.
    const hfs::HtmlDocument document{"<title>  Cats\n &amp;\t Dogs  </title><p>One<p>Two"};
    EXPECT_EQ(document.title(), "Cats & Dogs");
}

TEST(HtmlDocument, NumericEntitiesProduceUtf8)
{
    const hfs::HtmlDocument document{"<title>Caf&#233; &#x1F431;</title>"};
    EXPECT_EQ(document.title(), "Caf\xC3\xA9 \xF0\x9F\x90\xB1");
}

TEST(HtmlDocument, DoesNotBorrowInputBuffer)
{
    std::string source{"<title>Independent title</title>"};
    const hfs::HtmlDocument document{source};
    source.assign(source.size(), 'x');
    EXPECT_EQ(document.title(), "Independent title");
}

TEST(HtmlDocument, ReturnedTitleOutlivesDocument)
{
    std::string saved_title;
    {
        const hfs::HtmlDocument document{"<title>Independent title</title>"};
        saved_title = document.title();
    }
    EXPECT_EQ(saved_title, "Independent title");
}

TEST(HtmlDocument, MoveConstructionTransfersDocument)
{
    hfs::HtmlDocument original{"<title>Moved title</title>"};
    auto moved = std::move(original);
    EXPECT_EQ(moved.title(), "Moved title");
    EXPECT_TRUE(original.title().empty());
}

TEST(HtmlDocument, MoveAssignmentReplacesDocument)
{
    hfs::HtmlDocument source{"<title>Moved title</title>"};
    hfs::HtmlDocument target{"<title>Old title</title>"};
    target = std::move(source);
    EXPECT_EQ(target.title(), "Moved title");
    EXPECT_TRUE(source.title().empty());
}

TEST(HtmlDocument, EmptyAndHeadOnlyDocumentsHaveNoBodyText)
{
    EXPECT_TRUE(hfs::HtmlDocument{std::string_view{}}.body_text().empty());
    EXPECT_TRUE(hfs::HtmlDocument{"<title>Not body text</title>"}.body_text().empty());
    EXPECT_TRUE(hfs::HtmlDocument{"<body> \n\t&nbsp; "}.body_text().empty());
}

TEST(HtmlDocument, InlineMarkupPreservesWordBoundaries)
{
    const hfs::HtmlDocument document{"<p>Hel<b>lo</b>, <em>world</em>!</p>"};
    EXPECT_EQ(document.body_text(), "Hello, world!");
}

TEST(HtmlDocument, BlockElementsAndBreaksSeparateText)
{
    const hfs::HtmlDocument document{
        "<div><h1>Heading</h1><p>First<br>line</p></div>"
        "<ul><li>One<li>Two</ul><table><tr><td>A<td>B</table>Tail"};
    EXPECT_EQ(document.body_text(), "Heading\nFirst\nline\nOne\nTwo\nA\nB\nTail");
}

TEST(HtmlDocument, BodyWhitespaceAndEntitiesAreNormalized)
{
    const hfs::HtmlDocument document{
        "<p>  Cats\t &amp;\n Dogs&nbsp; &#233; &#x1F431;  </p>"};
    EXPECT_EQ(document.body_text(), "Cats & Dogs \xC3\xA9 \xF0\x9F\x90\xB1");
}

TEST(HtmlDocument, ScriptsStylesTemplatesAndCommentsAreSkipped)
{
    const hfs::HtmlDocument document{
        "<body>Before<script>secret()</script><style>.hidden{}</style>"
        "<template><p>Not rendered</p></template><!-- comment --> After"};
    EXPECT_EQ(document.body_text(), "Before After");
}

TEST(HtmlDocument, BodyTextRemainsIndependentAfterDocumentDestruction)
{
    std::string text;
    {
        const hfs::HtmlDocument document{"<p>Saved text"};
        text = document.body_text();
        EXPECT_EQ(document.body_text(), text);
    }
    EXPECT_EQ(text, "Saved text");
}

TEST(HtmlDocument, BodyTextFollowsDocumentMoves)
{
    hfs::HtmlDocument source{"<p>Transferred text"};
    auto target = std::move(source);
    EXPECT_EQ(target.body_text(), "Transferred text");
    EXPECT_TRUE(source.body_text().empty());
}

TEST(HtmlDocument, DeepNestingDoesNotRequireRecursiveTraversal)
{
    std::string html;
    for (int depth = 0; depth < 2000; ++depth) {
        html += "<div>";
    }
    html += "Deep text";
    EXPECT_EQ(hfs::HtmlDocument{html}.body_text(), "Deep text");
}

TEST(HtmlDocument, LinksPreserveDocumentOrderAndDuplicates)
{
    const hfs::HtmlDocument document{
        "<a href='/first'>First</a><div><a href='../second'>Second</a></div>"
        "<map><area href='/first'></map>"};
    EXPECT_EQ(document.links(), (std::vector<std::string>{"/first", "../second", "/first"}));
}

TEST(HtmlDocument, LinksDistinguishMissingAndEmptyHref)
{
    const hfs::HtmlDocument document{"<a>No href</a><a href=''>Empty</a><area href>"};
    EXPECT_EQ(document.links(), (std::vector<std::string>{"", ""}));
    EXPECT_TRUE(hfs::HtmlDocument{std::string_view{}}.links().empty());
}

TEST(HtmlDocument, LinksDecodeEntitiesWithoutNormalizingUrls)
{
    const hfs::HtmlDocument document{
        "<base href='https://example.org/'><A HREF=' /search?a=1&amp;b=2 '>Search</A>"
        "<a href='#part'>Fragment</a><a href='mailto:a@example.org'>Mail</a>"
        "<a href='javascript:void(0)'>Action</a>"};
    EXPECT_EQ(document.links(), (std::vector<std::string>{
        " /search?a=1&b=2 ", "#part", "mailto:a@example.org", "javascript:void(0)"}));
}

TEST(HtmlDocument, LinksIgnoreResourcesTemplatesAndForeignAnchors)
{
    const hfs::HtmlDocument document{
        "<link href='/style.css'><body><img src='/image.png'>"
        "<template><a href='/inert'>Inert</a></template>"
        "<script>const html = '<a href=\"/script\">';</script>"
        "<!-- <a href='/comment'> -->"
        "<svg><a href='/svg'>SVG</a></svg><a href='/page'>Page</a>"};
    EXPECT_EQ(document.links(), (std::vector<std::string>{"/page"}));
}

TEST(HtmlDocument, LinksOutliveDocument)
{
    std::vector<std::string> links;
    {
        const hfs::HtmlDocument document{"<a href='/saved'>Saved</a>"};
        links = document.links();
        EXPECT_EQ(document.links(), links);
    }
    EXPECT_EQ(links, (std::vector<std::string>{"/saved"}));
}

TEST(HtmlDocument, LinksFollowDocumentMoves)
{
    hfs::HtmlDocument source{"<a href='/moved'>Moved</a>"};
    auto target = std::move(source);
    EXPECT_TRUE(source.links().empty());
    EXPECT_EQ(target.links(), (std::vector<std::string>{"/moved"}));
}
