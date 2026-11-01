#include "hfs/html_document.hpp"

#include <lexbor/html/interfaces/document.h>
#include <lexbor/dom/interfaces/character_data.h>

#include <cstdint>
#include <stdexcept>

namespace hfs {

namespace {

auto is_block(std::uintptr_t tag) -> bool
{
    switch (tag) {
    case LXB_TAG_ADDRESS: case LXB_TAG_ARTICLE: case LXB_TAG_ASIDE:
    case LXB_TAG_BLOCKQUOTE: case LXB_TAG_BR: case LXB_TAG_DIV:
    case LXB_TAG_DL: case LXB_TAG_DT: case LXB_TAG_DD:
    case LXB_TAG_FIGCAPTION: case LXB_TAG_FIGURE: case LXB_TAG_FOOTER:
    case LXB_TAG_FORM: case LXB_TAG_H1: case LXB_TAG_H2: case LXB_TAG_H3:
    case LXB_TAG_H4: case LXB_TAG_H5: case LXB_TAG_H6: case LXB_TAG_HEADER:
    case LXB_TAG_HR: case LXB_TAG_LI: case LXB_TAG_MAIN: case LXB_TAG_NAV:
    case LXB_TAG_OL: case LXB_TAG_P: case LXB_TAG_PRE: case LXB_TAG_SECTION:
    case LXB_TAG_TABLE: case LXB_TAG_TD: case LXB_TAG_TH: case LXB_TAG_TR:
    case LXB_TAG_UL:
        return true;
    default:
        return false;
    }
}

// Delay separators until another character arrives, avoiding leading/trailing space.
class BodyText {
public:
    std::string value;

    void boundary() { separator_ = '\n'; }

    void append(std::string_view text)
    {
        for (std::size_t index = 0; index < text.size(); ++index) {
            const char character = text[index];
            const bool nbsp = text.substr(index, 2) == "\xC2\xA0";
            if (character == ' ' || character == '\t' || character == '\n' ||
                character == '\r' || character == '\f' || nbsp) {
                if (separator_ == '\0') {
                    separator_ = ' ';
                }
                if (nbsp) {
                    ++index;
                }
                continue;
            }
            if (separator_ != '\0' && !value.empty()) {
                value += separator_;
            }
            separator_ = '\0';
            value += character;
        }
    }

private:
    char separator_{};
};

} // namespace

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

auto HtmlDocument::body_text() const -> std::string
{
    if (!impl_) {
        return {};
    }
    auto* body = lxb_html_document_body_element(impl_->document.get());
    if (body == nullptr) {
        return {};
    }

    BodyText output;
    auto* root = lxb_dom_interface_node(body);
    auto* node = root->first_child;
    // Walk parent/sibling links instead of recursing on untrusted HTML depth.
    while (node != nullptr) {
        const bool element = node->type == LXB_DOM_NODE_TYPE_ELEMENT;
        const bool skip = element && (node->local_name == LXB_TAG_SCRIPT ||
            node->local_name == LXB_TAG_STYLE || node->local_name == LXB_TAG_TEMPLATE);
        if (element && is_block(node->local_name)) {
            output.boundary();
        }
        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            const auto& data = lxb_dom_interface_character_data(node)->data;
            if (data.length != 0) {
                output.append({reinterpret_cast<const char*>(data.data), data.length});
            }
        }
        if (!skip && node->first_child != nullptr) {
            node = node->first_child;
            continue;
        }
        while (node->next == nullptr) {
            node = node->parent;
            if (node == root) {
                return output.value;
            }
            if (node->type == LXB_DOM_NODE_TYPE_ELEMENT && is_block(node->local_name)) {
                output.boundary();
            }
        }
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT && is_block(node->local_name)) {
            output.boundary();
        }
        node = node->next;
    }
    return output.value;
}

} // namespace hfs
