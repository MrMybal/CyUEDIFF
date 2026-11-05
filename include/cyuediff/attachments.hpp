#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
struct AttachmentOptions {
    std::size_t max_owners = 1024, max_sockets = 100000, max_references = 100000, max_virtual_bones = 100000,
                max_reference_bones = 100000, max_nodes = 1000000, max_text_bytes = 16 * 1024 * 1024;
    unsigned max_depth = 48;
    bool source_indices = true;
    OperationControl control;
};
// Stored reflected socket objects and explicit owner lists. No external loading or class defaults.
Json attachment_snapshot(const Package &, const AttachmentOptions & = {});
struct AttachmentComparison {
    Json changes = Json::array();
    bool complete = false;
    std::string status() const;
    Json to_json() const;
};
AttachmentComparison compare_attachments(const Package &, const Package &, const AttachmentOptions & = {});
} // namespace cyuediff
