#include "hfs/html_document.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<hfs::HtmlDocument>);
static_assert(!std::is_copy_assignable_v<hfs::HtmlDocument>);
static_assert(std::is_nothrow_move_constructible_v<hfs::HtmlDocument>);
static_assert(std::is_nothrow_move_assignable_v<hfs::HtmlDocument>);

auto main() -> int
try {
    int failures{};
    const auto check = [&failures](bool passed, const char* message) {
        // Unlike assert, these checks also run in Release builds.
        if (!passed) {
            std::cerr << message << '\n';
            ++failures;
        }
    };

    check(hfs::HtmlDocument{std::string_view{}}.title().empty(), "Empty input needs no title");
    check(hfs::HtmlDocument{"<p>No title"}.title().empty(), "Missing title must be empty");
    check(hfs::HtmlDocument{"<title></title>"}.title().empty(), "Empty title must be empty");
    check(hfs::HtmlDocument{"<title>  Cats\n &amp;\t Dogs  </title><p>One<p>Two"}.title()
              == "Cats & Dogs", "Title whitespace and entities must be normalized");
    check(hfs::HtmlDocument{"<title>Caf&#233; &#x1F431;</title>"}.title()
              == "Caf\xC3\xA9 \xF0\x9F\x90\xB1", "Numeric entities must produce UTF-8");

    std::string saved_title;
    {
        std::string source{"<title>Independent title</title>"};
        const hfs::HtmlDocument document{source};
        source.assign(source.size(), 'x');
        saved_title = document.title();
        check(saved_title == "Independent title", "DOM must not borrow the input buffer");
    }
    check(saved_title == "Independent title", "Returned title must outlive the DOM");

    hfs::HtmlDocument original{"<title>Moved title</title>"};
    auto moved = std::move(original);
    check(moved.title() == "Moved title", "Move construction must transfer the DOM");
    check(original.title().empty(), "Moved-from document must have an empty title");
    hfs::HtmlDocument target{"<title>Old title</title>"};
    target = std::move(moved);
    check(target.title() == "Moved title", "Move assignment must replace the old DOM");
    check(moved.title().empty(), "Move-assigned source must have an empty title");

    return failures == 0 ? 0 : 1;
} catch (const std::exception& error) {
    std::cerr << "Unexpected exception: " << error.what() << '\n';
    return 1;
}
