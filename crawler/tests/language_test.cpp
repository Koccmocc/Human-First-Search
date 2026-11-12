#include "hfs/language.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

constexpr std::string_view english{
    "The public library opens its doors every morning and welcomes readers of all ages. "
    "Visitors can borrow books, study local history, or attend a workshop with their neighbours. "
    "Staff members help students find reliable information and explain how to use the catalogue. "
    "The reading room offers a quiet place to think, learn, and discover new ideas."};

void expect_unknown(std::string_view text, hfs::LanguageStatus status)
{
    const auto result = hfs::detect_language(text);
    EXPECT_EQ(result.status, status);
    EXPECT_FALSE(result.code);
    EXPECT_EQ(result.percent, 0);
}

} // namespace

TEST(Language, DetectsEnglishAndReturnsOwningMetadata)
{
    std::string input{english};
    const auto result = hfs::detect_language(input);
    input.assign("Input released");
    EXPECT_EQ(result.status, hfs::LanguageStatus::detected);
    EXPECT_EQ(result.code, "en");
    EXPECT_GE(result.percent, 90);
    EXPECT_LE(result.percent, 100);
}

TEST(Language, ReportsNonEnglishCodesWithoutAnEnglishBias)
{
    constexpr std::string_view german{
        "Die Bibliothek ist ein wichtiger Treffpunkt in unserer Stadt. Jeden Morgen kommen "
        "Menschen hierher, um gemeinsam zu lesen und neue Informationen zu finden. Die Mitarbeiter "
        "helfen bei der Suche nach passenden Zeitungen und wissenschaftlichen Arbeiten. "
        "Kinder lernen die Geschichten verschiedener Menschen kennen und entwickeln eigene Ideen. "
        "In einem ruhigen Raum kann jeder arbeiten, nachdenken und sich auf seine Aufgaben konzentrieren."};
    const auto de = hfs::detect_language(german);
    EXPECT_EQ(de.status, hfs::LanguageStatus::detected);
    EXPECT_EQ(de.code, "de");

    constexpr std::u8string_view ukrainian{
        u8"Міська бібліотека щодня запрошує відвідувачів читати книжки та дізнаватися нове. "
        u8"Працівники допомагають школярам знаходити потрібну інформацію й пояснюють, як працює каталог. "
        u8"У читальній залі можна спокійно підготуватися до занять або дослідити історію свого міста. "
        u8"Для дітей організовують зустрічі з українськими письменниками та обговорення улюблених творів. "
        u8"Такі заходи розвивають уяву й підтримують бажання навчатися протягом усього життя."};
    const auto uk = hfs::detect_language({reinterpret_cast<const char*>(ukrainian.data()), ukrainian.size()});
    EXPECT_EQ(uk.status, hfs::LanguageStatus::detected);
    EXPECT_EQ(uk.code, "uk");
}

TEST(Language, UsesUnicodeScalarCountForTheMinimumLength)
{
    expect_unknown({}, hfs::LanguageStatus::too_short);
    expect_unknown("Hello, world!", hfs::LanguageStatus::too_short);
    expect_unknown(english.substr(0, hfs::language_min_characters - 1), hfs::LanguageStatus::too_short);
    for (const auto letter : {std::string_view{"\xC3\xA9"}, std::string_view{"\xF0\x9F\x98\x80"}}) {
        std::string input;
        for (std::size_t index = 0; index < hfs::language_min_characters - 1; ++index) {
            input.append(letter);
        }
        expect_unknown(input, hfs::LanguageStatus::too_short);
        input.append(letter);
        // At exactly 200 scalars the detector decides; the byte count is not the gate.
        const auto result = hfs::detect_language(input);
        EXPECT_TRUE(result.status == hfs::LanguageStatus::detected ||
                    result.status == hfs::LanguageStatus::unreliable);
    }
}

TEST(Language, RejectsMalformedUtf8AndNulBeforeReturningShortOrDetected)
{
    for (const auto invalid : {std::string_view{"\xFF"}, std::string_view{"\xC0\xAF"},
                              std::string_view{"\xED\xA0\x80"}, std::string_view{"\xF4\x90\x80\x80"},
                              std::string_view{"\xE2\x82"}, std::string_view{"bad\0tail", 8}}) {
        expect_unknown(invalid, hfs::LanguageStatus::invalid_text);
        // Validate the entire buffer, including corruption after readable English.
        expect_unknown(std::string{english} + std::string{invalid}, hfs::LanguageStatus::invalid_text);
    }
}

TEST(Language, DoesNotInventALanguageForNonProse)
{
    expect_unknown(std::string(300, ' '), hfs::LanguageStatus::unreliable);
    std::string numbers;
    for (int index = 0; index < 50; ++index) {
        numbers += "12345 -- ";
    }
    expect_unknown(numbers, hfs::LanguageStatus::unreliable);
}

TEST(Language, EnforcesTheByteCapWithoutTruncatingOrGuessingFromAPrefix)
{
    const std::string oversized(hfs::language_max_bytes + 1, '\xFF');
    expect_unknown(oversized, hfs::LanguageStatus::too_large);
    std::string at_limit(hfs::language_max_bytes, ' ');
    expect_unknown(at_limit, hfs::LanguageStatus::unreliable);
    at_limit.back() = '\xFF';
    expect_unknown(at_limit, hfs::LanguageStatus::invalid_text);
}
