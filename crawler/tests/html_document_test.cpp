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

TEST(HtmlDocument, MainTextPrefersMainAndKeepsContentHeadings)
{
    const hfs::HtmlDocument document{
        "<header>Site header</header><article>Earlier article</article>"
        "<main><header><h1>Heading</h1></header><p>Main <b>content</b></p>"
        "<nav>Menu</nav><aside>Advert</aside><footer>Related</footer>"
        "<p hidden>Hidden text</p><p>Tail</p></main><main>Later main</main>"};
    const auto selected = document.main_text();
    EXPECT_EQ(selected.source, hfs::HtmlTextSource::main);
    EXPECT_EQ(selected.text, "Heading\nMain content\nTail");
    EXPECT_NE(document.body_text().find("Site header"), std::string::npos);
    EXPECT_NE(document.body_text().find("Advert"), std::string::npos);
}

TEST(HtmlDocument, MainTextFallsBackToFirstNonemptyArticle)
{
    const hfs::HtmlDocument document{
        "<main><nav>Menu only</nav></main><article hidden>Hidden article</article>"
        "<article> &nbsp;<script>ignored()</script></article>"
        "<article><h1>Article heading</h1><p>Caf&#233; &amp; tea</p>"
        "<header>Byline</header></article><article>Later article</article>"};
    const auto selected = document.main_text();
    EXPECT_EQ(selected.source, hfs::HtmlTextSource::article);
    EXPECT_EQ(selected.text, "Article heading\nCaf\xC3\xA9 & tea\nByline");
}

TEST(HtmlDocument, MainRoleSelectsContentWithoutOuterSiteChrome)
{
    const hfs::HtmlDocument document{
        "<header>Site controls</header><p>Service notice</p>"
        "<section role=' \tmain\n'><h1>Heading</h1><p>Main content</p>"
        "<nav>Menu</nav><aside>Advert</aside><p hidden>Hidden</p></section>"
        "<footer>Site footer</footer>"};
    const auto selected = document.main_text();
    EXPECT_EQ(selected.source, hfs::HtmlTextSource::main_role);
    EXPECT_EQ(selected.text, "Heading\nMain content");
    EXPECT_NE(document.body_text().find("Site controls"), std::string::npos);
}

TEST(HtmlDocument, MainRolePreservesNativeMainAndArticlePriority)
{
    const hfs::HtmlDocument with_main{
        "<section role=main>Role content</section><article>Article</article><main>Native main</main>"};
    EXPECT_EQ(with_main.main_text().source, hfs::HtmlTextSource::main);
    EXPECT_EQ(with_main.main_text().text, "Native main");
    const hfs::HtmlDocument with_article{
        "<section role=main>Role content</section><main> </main><article>Article</article>"};
    EXPECT_EQ(with_article.main_text().source, hfs::HtmlTextSource::article);
    EXPECT_EQ(with_article.main_text().text, "Article");
}

TEST(HtmlDocument, MainRoleSkipsHiddenInertForeignAndEmptyRegions)
{
    const hfs::HtmlDocument document{
        "<nav><div role=main>Menu candidate</div></nav><div role=main hidden>Hidden</div>"
        "<template><div role=main>Inert</div></template><svg><g role=main>Foreign</g></svg>"
        "<div role=main><nav>Only menu</nav></div><section role=main><p>Real text</p></section>"};
    EXPECT_EQ(document.main_text().source, hfs::HtmlTextSource::main_role);
    EXPECT_EQ(document.main_text().text, "Real text");
    const hfs::HtmlDocument body_role{"<body role=main><p>Body region</p>"};
    EXPECT_EQ(body_role.main_text().source, hfs::HtmlTextSource::main_role);
    EXPECT_EQ(body_role.main_text().text, "Body region");
    EXPECT_EQ(hfs::HtmlDocument{"<body role=main> "}.main_text().source, hfs::HtmlTextSource::body);
}

TEST(HtmlDocument, MainRoleDoesNotGuessFromRoleListsOrSimilarValues)
{
    for (const auto* role : {"", "MAIN", "mainly", "navigation main", "main navigation", "unknown main"}) {
        const hfs::HtmlDocument document{"<section role='" + std::string{role} + "'>Fallback text</section>"};
        EXPECT_EQ(document.main_text().source, hfs::HtmlTextSource::body);
        EXPECT_EQ(document.main_text().text, "Fallback text");
    }
}

TEST(HtmlDocument, HeadingFallbackRemovesPreludeAndPreservesNormalizationAndDiscovery)
{
    const std::string prose(200, 'x');
    const hfs::HtmlDocument document{"<div>Site menu<a href='/outside'>Outside</a></div>"
        "<h1>Page &amp; title</h1><p> " + prose + " &nbsp; Caf&#233;</p>"
        "<nav>Navigation</nav><footer>Footer</footer><p hidden>Hidden</p>"};
    const auto selected = document.main_text();
    EXPECT_EQ(selected.source, hfs::HtmlTextSource::heading);
    EXPECT_EQ(selected.text, "Page & title\n" + prose + " Caf\xC3\xA9");
    EXPECT_NE(document.body_text().find("Site menu"), std::string::npos);
    EXPECT_EQ(document.links(), (std::vector<std::string>{"/outside"}));
}

TEST(HtmlDocument, HeadingFallbackStaysInsideItsImmediateContainer)
{
    for (const auto* tag : {"div", "section"}) {
        const std::string prose(200, 'p');
        const hfs::HtmlDocument document{"<p>Outer prelude</p><" + std::string{tag} + ">"
            "<p>Inner prelude</p><h1>Heading</h1><p>" + prose + "</p></" + tag + "><p>Outer tail</p>"};
        EXPECT_EQ(document.main_text().source, hfs::HtmlTextSource::heading);
        EXPECT_EQ(document.main_text().text, "Heading\n" + prose);
    }
}

TEST(HtmlDocument, HeadingFallbackKeepsNativeAndRoleContainerPriority)
{
    for (const auto* container : {"main", "article", "section role=main"}) {
        const std::string opening{container};
        const auto tag = opening.substr(0, opening.find(' '));
        const hfs::HtmlDocument document{"<h1>Fallback heading</h1><p>" + std::string(200, 'p') +
            "</p><" + opening + ">Semantic content</" + tag + ">"};
        EXPECT_NE(document.main_text().source, hfs::HtmlTextSource::heading);
        EXPECT_EQ(document.main_text().text, "Semantic content");
    }
}

TEST(HtmlDocument, HeadingFallbackCountsOnlyHeadingsOutsideSkippedSubtrees)
{
    const std::string prose(200, 'p');
    const hfs::HtmlDocument document{"<nav><h1>Menu</h1></nav><div hidden><h1>Hidden</h1></div>"
        "<template><h1>Inert</h1></template><h1>Visible</h1><p>" + prose + "</p>"};
    EXPECT_EQ(document.main_text().source, hfs::HtmlTextSource::heading);
    EXPECT_EQ(document.main_text().text, "Visible\n" + prose);
}

TEST(HtmlDocument, HeadingFallbackRequiresAnUnambiguousHeadingWithSubstantialFollowingText)
{
    const std::string prose(200, 'p');
    for (const auto& html : {"<h1>First</h1><p>" + prose + "</p><h1>Second</h1>",
                            "<div><h1>Title wrapper only</h1></div><p>" + prose + "</p>",
                            "<h1> </h1><p>" + prose + "</p>",
                            "<ul><li><h1>List heading</h1><p>" + prose + "</p></li></ul>",
                            "<h1>Long heading " + prose + "</h1><p>Short teaser</p>"}) {
        const hfs::HtmlDocument document{html};
        EXPECT_EQ(document.main_text().source, hfs::HtmlTextSource::body);
    }
    const hfs::HtmlDocument short_text{"<h1>Heading</h1><p>" + std::string(199, 'p') + "</p>"
        "<nav>" + prose + "</nav><p hidden>" + prose + "</p>"};
    EXPECT_EQ(short_text.main_text().source, hfs::HtmlTextSource::body);
}

TEST(HtmlDocument, HeadingFallbackThresholdMeasuresNormalizedUtf8Bytes)
{
    std::string text;
    for (int count = 0; count < 100; ++count) {
        text += "\xC3\xA9";
    }
    const hfs::HtmlDocument document{"<p>Prelude</p><h1>Heading</h1><p> \t" + text + " &nbsp;</p>"};
    EXPECT_EQ(document.main_text().source, hfs::HtmlTextSource::heading);
    EXPECT_EQ(document.main_text().text, "Heading\n" + text);
}

TEST(HtmlDocument, HeadingFallbackSupportsDeepContainersIteratively)
{
    std::string html;
    for (int depth = 0; depth < 2000; ++depth) {
        html += "<div>";
    }
    html += "<h1>Deep heading</h1><p>" + std::string(200, 'p') + "</p>";
    for (int depth = 0; depth < 2000; ++depth) {
        html += "</div>";
    }
    const auto selected = hfs::HtmlDocument{html}.main_text();
    EXPECT_EQ(selected.source, hfs::HtmlTextSource::heading);
    EXPECT_EQ(selected.text, "Deep heading\n" + std::string(200, 'p'));
}

TEST(HtmlDocument, MainTextUsesFilteredBodyWhenNoUsableContainerExists)
{
    const hfs::HtmlDocument document{
        "<header>Site header</header><nav><main>Fake main</main></nav>"
        "<aside><article>Fake article</article></aside>"
        "<main> &nbsp; </main><article><script>ignored()</script></article>"
        "<section><h1>Heading</h1><p>Body &amp; text</p></section>"
        "<footer>Footer</footer><div hidden><main>Hidden main</main></div>"};
    const auto selected = document.main_text();
    EXPECT_EQ(selected.source, hfs::HtmlTextSource::body);
    EXPECT_EQ(selected.text, "Site header\nHeading\nBody & text");
}

TEST(HtmlDocument, MainTextIgnoresInertAndForeignCandidates)
{
    const hfs::HtmlDocument document{
        "<template><main>Inert main</main></template><!-- <main>Comment</main> -->"
        "<svg><main>Foreign main</main><article>Foreign article</article></svg>"
        "<article>Real &amp; content</article>"};
    const auto selected = document.main_text();
    EXPECT_EQ(selected.source, hfs::HtmlTextSource::article);
    EXPECT_EQ(selected.text, "Real & content");
}

TEST(HtmlDocument, MainTextSupportsDeepNonemptyAndEmptyCandidatesIteratively)
{
    std::string opening;
    std::string closing;
    for (int depth = 0; depth < 2000; ++depth) {
        opening += "<main>";
        closing += "</main>";
    }
    const auto nonempty = hfs::HtmlDocument{opening + "Deep text" + closing}.main_text();
    EXPECT_EQ(nonempty.source, hfs::HtmlTextSource::main);
    EXPECT_EQ(nonempty.text, "Deep text");
    const auto empty = hfs::HtmlDocument{opening + closing + "<article>Fallback article</article>"}.main_text();
    EXPECT_EQ(empty.source, hfs::HtmlTextSource::article);
    EXPECT_EQ(empty.text, "Fallback article");
}

TEST(HtmlDocument, MainTextOwnsItsResultAndHandlesMovesAndEmptyDocuments)
{
    std::string saved;
    {
        hfs::HtmlDocument source{"<main><p>Saved text</p></main>"};
        auto target = std::move(source);
        EXPECT_TRUE(source.main_text().text.empty());
        saved = target.main_text().text;
    }
    EXPECT_EQ(saved, "Saved text");
    EXPECT_TRUE(hfs::HtmlDocument{std::string_view{}}.main_text().text.empty());
    EXPECT_TRUE(hfs::HtmlDocument{"<body hidden><main>Hidden body</main>"}.main_text().text.empty());
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
