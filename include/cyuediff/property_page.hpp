#pragma once
#include <cyuediff/visual_diff.hpp>
namespace cyuediff {
struct PropertyPageOptions {
    std::size_t first = 0, page_size = 128, max_compare_nodes = 4'000'000;
    bool serialization_fields = false, escape_non_bmp = false;
    OperationControl control;
    ValueCoverage coverage;
};
struct PropertyPage {
    std::string scope;
    std::size_t first = 0, total_children = 0;
    bool available = false, array = false, unwrapped = false;
    PropertyTable table;
    // Parallel to table.rows. Raw stored containers can be opened even if their typed value is scalar.
    std::vector<bool> expandable;
};
// A root summary and a bounded page of direct children; no input pointers survive this call.
// Row paths are actual JSON pointers, including stored /value wrappers. A missing scope is unavailable.
// serialization_fields=false unwraps a paired typed value for child enumeration; true exposes its tag fields.
// A null input pointer means known absence. Typed opaque/skipped ancestors preserve unknown values;
// use coverage for additional unread export/native scopes. Serialization metadata stays separately readable.
PropertyPage property_page(const Json *before, const Json *after, const std::string &scope = {},
                           const PropertyPageOptions &options = {});
} // namespace cyuediff
