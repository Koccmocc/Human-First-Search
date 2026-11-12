// The upstream public header declares FILE* and expects its caller to include stdio.
#include <cstdio>

#include <public/compact_lang_det.h>
#include <public/encodings.h>

#include <gtest/gtest.h>

#include <string_view>

namespace {

struct Detection {
    CLD2::Language language{CLD2::UNKNOWN_LANGUAGE};
    CLD2::Language top[3]{};
    int percent[3]{};
    double scores[3]{};
    int text_bytes{};
    bool reliable{};
    int valid_prefix{};
};

auto detect(std::string_view text) -> Detection
{
    // Test literals fit in int. The future public wrapper must check user input lengths.
    const CLD2::CLDHints hints{nullptr, nullptr, CLD2::UNKNOWN_ENCODING, CLD2::UNKNOWN_LANGUAGE};
    Detection result;
    // Extended checked API avoids the legacy API's forced-English fallback.
    result.language = CLD2::ExtDetectLanguageSummaryCheckUTF8(
        text.empty() ? "" : text.data(), static_cast<int>(text.size()), true, &hints, 0,
        result.top, result.percent, result.scores, nullptr,
        &result.text_bytes, &result.reliable, &result.valid_prefix);
    return result;
}

} // namespace

TEST(LanguageDetector, RecognizesEnglishPlainTextWithoutHints)
{
    constexpr std::string_view text{
        "The public library opens its doors every morning and welcomes readers of all ages. "
        "Visitors can borrow books, study local history, or attend a workshop with their neighbours. "
        "Staff members help students find reliable information and explain how to use the catalogue. "
        "The reading room offers a quiet place to think, learn, and discover new ideas."};
    const auto result = detect(text);
    EXPECT_EQ(result.language, CLD2::ENGLISH);
    EXPECT_EQ(result.top[0], CLD2::ENGLISH);
    EXPECT_STREQ(CLD2::LanguageCode(result.language), "en");
    EXPECT_TRUE(result.reliable);
    EXPECT_GE(result.percent[0], 90);
    EXPECT_EQ(result.valid_prefix, static_cast<int>(text.size()));
    EXPECT_GT(result.text_bytes, 0);
}

TEST(LanguageDetector, DistinguishesGermanFromEnglish)
{
    constexpr std::string_view text{
        "Die Bibliothek ist ein wichtiger Treffpunkt in unserer Stadt. Jeden Morgen kommen "
        "Menschen hierher, um gemeinsam zu lesen und neue Informationen zu finden. Die Mitarbeiter "
        "helfen bei der Suche nach passenden Zeitungen und wissenschaftlichen Arbeiten. "
        "Kinder lernen die Geschichten verschiedener Menschen kennen und entwickeln eigene Ideen. "
        "In einem ruhigen Raum kann jeder arbeiten, nachdenken und sich auf seine Aufgaben konzentrieren."};
    const auto result = detect(text);
    EXPECT_EQ(result.language, CLD2::GERMAN);
    EXPECT_STREQ(CLD2::LanguageCode(result.language), "de");
    EXPECT_TRUE(result.reliable);
    EXPECT_EQ(result.valid_prefix, static_cast<int>(text.size()));
}

TEST(LanguageDetector, RecognizesUkrainianUtf8)
{
    constexpr std::u8string_view utf8{
        u8"Міська бібліотека щодня запрошує відвідувачів читати книжки та дізнаватися нове. "
        u8"Працівники допомагають школярам знаходити потрібну інформацію й пояснюють, як працює каталог. "
        u8"У читальній залі можна спокійно підготуватися до занять або дослідити історію свого міста. "
        u8"Для дітей організовують зустрічі з українськими письменниками та обговорення улюблених творів. "
        u8"Такі заходи розвивають уяву й підтримують бажання навчатися протягом усього життя."};
    const std::string_view text{reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    const auto result = detect(text);
    EXPECT_EQ(result.language, CLD2::UKRAINIAN);
    EXPECT_STREQ(CLD2::LanguageCode(result.language), "uk");
    EXPECT_TRUE(result.reliable);
    EXPECT_EQ(result.valid_prefix, static_cast<int>(text.size()));
}

TEST(LanguageDetector, RecognizesJapaneseWithCompactCjkTables)
{
    constexpr std::u8string_view utf8{
        u8"図書館では、子供から大人まで多くの人が本を読んでいます。静かな部屋で勉強したり、"
        u8"地域の歴史について調べたりすることができます。職員は必要な資料を探す手伝いをします。"
        u8"週末には作家との交流会や子供向けのお話の時間も開催されます。新しい知識を学びたい人は、"
        u8"だれでも自由に参加することができます。図書館は地域の人々にとって大切な場所です。"};
    const std::string_view text{reinterpret_cast<const char*>(utf8.data()), utf8.size()};
    const auto result = detect(text);
    EXPECT_EQ(result.language, CLD2::JAPANESE);
    EXPECT_STREQ(CLD2::LanguageCode(result.language), "ja");
    EXPECT_TRUE(result.reliable);
    EXPECT_EQ(result.valid_prefix, static_cast<int>(text.size()));
}

TEST(LanguageDetector, DoesNotTreatEmptyOrAmbiguousTextAsReliableEnglish)
{
    const auto empty = detect({});
    EXPECT_EQ(empty.language, CLD2::UNKNOWN_LANGUAGE);
    EXPECT_FALSE(empty.reliable);
    EXPECT_EQ(empty.valid_prefix, 0);
    EXPECT_FALSE(detect("x").reliable);
}

TEST(LanguageDetector, CheckedApiRejectsMalformedUtf8)
{
    const auto result = detect("Valid prefix \xFF broken suffix");
    EXPECT_EQ(result.language, CLD2::UNKNOWN_LANGUAGE);
    EXPECT_FALSE(result.reliable);
    EXPECT_EQ(result.valid_prefix, 13);
}
