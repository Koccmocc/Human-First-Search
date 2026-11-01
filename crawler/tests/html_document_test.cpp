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
