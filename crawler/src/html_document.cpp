#include "hfs/html_document.hpp"

#include <lexbor/html/interfaces/document.h>

#include <stdexcept>

namespace hfs {

struct HtmlDocument::Impl {
    std::unique_ptr<lxb_html_document_t, decltype(&lxb_html_document_destroy)>
        document{lxb_html_document_create(), &lxb_html_document_destroy};
};

HtmlDocument::HtmlDocument(std::string_view html)
    : impl_{std::make_unique<Impl>()}
{
    if (!impl_->document) {
        throw std::runtime_error{"Could not allocate an HTML document"};
    }

    // A default string_view may have a null data pointer, even with zero length.
    const auto* bytes = reinterpret_cast<const lxb_char_t*>(html.empty() ? "" : html.data());
    const auto status = lxb_html_document_parse(impl_->document.get(), bytes, html.size());
    if (status != LXB_STATUS_OK) {
        // impl_ releases the DOM even when construction exits with an exception.
        throw std::runtime_error{"Could not parse HTML document"};
    }
}

// Define these where Impl is complete: unique_ptr needs its destructor here.
HtmlDocument::~HtmlDocument() = default;
HtmlDocument::HtmlDocument(HtmlDocument&&) noexcept = default;
auto HtmlDocument::operator=(HtmlDocument&&) noexcept -> HtmlDocument& = default;

auto HtmlDocument::title() const -> std::string
{
    if (!impl_) {
        return {};
    }

    std::size_t length{};
    const auto* text = lxb_html_document_title(impl_->document.get(), &length);
    if (text == nullptr) {
        return {};
    }

    // Lexbor owns text; copy it before the document can be destroyed or moved.
    return {reinterpret_cast<const char*>(text), length};
}

} // namespace hfs
