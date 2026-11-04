#pragma once
#include <cyuediff/core.hpp>
#include <map>
namespace cyuediff {
enum class ChangeKind { unchanged, added, removed, modified };
const char *change_name(ChangeKind);
// A null pointer represents absence, distinct from a present JSON null.
ChangeKind classify_change(const Json *before, const Json *after);
struct ChangeCounts {
    std::size_t unchanged = 0, added = 0, removed = 0, modified = 0;
    void add(ChangeKind);
    std::size_t changed() const {
        return added + removed + modified;
    }
};
struct GraphChanges {
    std::map<std::string, ChangeKind> nodes, links;
    ChangeCounts node_counts, link_counts;
};
// Snapshot graph keys provide identity. Changed connections mark their endpoints.
GraphChanges graph_changes(const Json *before, const Json *after);
struct PropertyRow {
    std::string path, type, before, after;
    ChangeKind change = ChangeKind::unchanged;
    unsigned depth = 0;
    bool before_present = false, after_present = false;
    std::string label;
};
struct PropertyTableOptions {
    std::size_t max_rows = 20000, max_text_bytes = 2048;
    unsigned max_depth = 48;
};
struct PropertyTable {
    std::vector<PropertyRow> rows;
    bool truncated = false;
};
// Bounded typed Unreal properties and native/opaque metadata, prepared off-frame.
// JSON-pointer paths remain unambiguous for field names containing '/' or '~'.
PropertyTable property_table(const Json *before, const Json *after, const PropertyTableOptions &options = {});
} // namespace cyuediff
