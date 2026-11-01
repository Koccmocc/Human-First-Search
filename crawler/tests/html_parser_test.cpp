#include <lexbor/html/interfaces/document.h>

#include <iostream>
#include <memory>
#include <string_view>

auto main() -> int
{
    // Keep the C document owned even when a check returns early.
    const std::unique_ptr<lxb_html_document_t, decltype(&lxb_html_document_destroy)>
        document{lxb_html_document_create(), &lxb_html_document_destroy};
    if (!document) {
        std::cerr << "Could not allocate an HTML document\n";
        return 1;
    }

    // Omitted closing tags are valid parser input, not a reason to reject a page.
    constexpr std::string_view html{
        "<title>Cats &amp; Dogs</title><body><p>First<p>Second"};
    const auto status = lxb_html_document_parse(
        document.get(), reinterpret_cast<const lxb_char_t*>(html.data()), html.size());
    if (status != LXB_STATUS_OK || lxb_html_document_body_element(document.get()) == nullptr) {
        std::cerr << "HTML parsing did not produce a body\n";
        return 1;
    }

    std::size_t length{};
    const auto* title = lxb_html_document_title(document.get(), &length);
    if (title == nullptr ||
        std::string_view{reinterpret_cast<const char*>(title), length} != "Cats & Dogs") {
        std::cerr << "The parsed title did not decode &amp; correctly\n";
        return 1;
    }
    return 0;
}
