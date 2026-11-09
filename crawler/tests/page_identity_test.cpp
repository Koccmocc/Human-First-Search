#include "hfs/page_identity.hpp"
#include "hfs/html_document.hpp"

#include <gtest/gtest.h>

#include <array>
#include <utility>

TEST(PageIdentity, MatchesKnownSha256VectorsIncludingEmptyAndMultiBlockInputs)
{
    const std::array vectors{
        std::pair{std::string{}, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        std::pair{std::string{"abc"}, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        std::pair{std::string{"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"},
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        std::pair{std::string(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"}};
    for (const auto& [text, expected] : vectors) {
        SCOPED_TRACE(text.size());
        const auto identity = hfs::make_page_identity("https://example.org/", text);
        ASSERT_TRUE(identity);
        EXPECT_EQ(identity->text_hash, "sha256:" + std::string{expected});
    }
}

TEST(PageIdentity, HashesUtf8AndEmbeddedNulByByteLength)
{
    const auto binary = hfs::make_page_identity("https://example.org/", std::string_view{"a\0b", 3});
    ASSERT_TRUE(binary);
    EXPECT_EQ(binary->text_hash, "sha256:59b271ae1bbcb1d31d41929817f4b16fb439eb4f31520b5ad1d5ce98920a7138");
    const auto utf8 = hfs::make_page_identity("https://example.org/", "Caf\xC3\xA9");
    ASSERT_TRUE(utf8);
    EXPECT_EQ(utf8->text_hash, "sha256:73473dcc12b763085904a5279d048c4d5b3b008c46f1f32443b99de04aa83a14");
    const auto decomposed = hfs::make_page_identity("https://example.org/", "Cafe\xCC\x81");
    ASSERT_TRUE(decomposed);
    EXPECT_NE(utf8->text_hash, decomposed->text_hash); // No implicit Unicode normalization.
}

TEST(PageIdentity, UsesCanonicalUrlAndMatchesAnIndependentUrlDigest)
{
    const auto identity = hfs::make_page_identity("https://EXAMPLE.org:443/a/../article#part", "Text");
    ASSERT_TRUE(identity);
    EXPECT_EQ(identity->page_id, "sha256:9eba6b3c57ce169e19742b249a7f7542a620751a5c4b2f2c4990684f7e655f70");
    const auto canonical = hfs::make_page_identity("https://example.org/article", "Text");
    ASSERT_TRUE(canonical);
    EXPECT_EQ(identity->page_id, canonical->page_id);
    EXPECT_EQ(identity->text_hash, canonical->text_hash);
}

TEST(PageIdentity, SeparatesPageIdentityFromContentChangesAndTextDuplicates)
{
    const auto original = hfs::make_page_identity("https://example.org/a", "First text");
    const auto changed = hfs::make_page_identity("https://example.org/a", "Changed text");
    const auto elsewhere = hfs::make_page_identity("https://example.org/b", "First text");
    ASSERT_TRUE(original && changed && elsewhere);
    EXPECT_EQ(original->page_id, changed->page_id);
    EXPECT_NE(original->text_hash, changed->text_hash);
    EXPECT_NE(original->page_id, elsewhere->page_id);
    EXPECT_EQ(original->text_hash, elsewhere->text_hash);
    const auto spaced = hfs::make_page_identity("https://example.org/a", "First  text");
    ASSERT_TRUE(spaced);
    EXPECT_NE(original->text_hash, spaced->text_hash); // Extraction owns whitespace rules.
}

TEST(PageIdentity, RetainsUrlModuleDistinctionsForPathsQueriesAndOrigins)
{
    const auto original = hfs::make_page_identity("https://example.org/a?x=1&y=2", "Text");
    ASSERT_TRUE(original);
    for (const auto* url : {"https://example.org/A?x=1&y=2", "https://example.org/a?y=2&x=1",
                            "http://example.org/a?x=1&y=2", "https://example.org:8443/a?x=1&y=2"}) {
        const auto other = hfs::make_page_identity(url, "Text");
        ASSERT_TRUE(other);
        EXPECT_NE(original->page_id, other->page_id);
        EXPECT_EQ(original->text_hash, other->text_hash);
    }
}

TEST(PageIdentity, RejectsUrlsThatCannotIdentifyAnHttpPage)
{
    for (const auto* url : {"", "relative", "file:///unused", "https://user:pass@example.org/", "http://[broken/"}) {
        EXPECT_FALSE(hfs::make_page_identity(url, "Text"));
    }
}

TEST(PageIdentity, HashesExtractedBodyInsteadOfMarkupOrTitle)
{
    const hfs::HtmlDocument first{"<title>First</title><p>Hello &amp; world</p>"};
    const hfs::HtmlDocument second{"<title>Second</title><div>Hello &amp; <b>world</b></div><script>noise</script>"};
    const auto original = hfs::make_page_identity("https://example.org/", first.body_text());
    const auto equivalent = hfs::make_page_identity("https://example.org/", second.body_text());
    ASSERT_TRUE(original && equivalent);
    EXPECT_EQ(original->text_hash, equivalent->text_hash);
    EXPECT_EQ(original->page_id, equivalent->page_id);
}
