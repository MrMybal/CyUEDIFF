#include <cyuediff/property_page.hpp>
#include <iostream>
using namespace cyuediff;
namespace {
unsigned checks = 0;
void check(bool ok, const char *why) {
    ++checks;
    if (!ok)
        throw std::runtime_error(why);
}
const PropertyRow &row(const PropertyTable &t, const std::string &path) {
    for (const auto &r : t.rows)
        if (r.path == path)
            return r;
    throw std::runtime_error("Missing row " + path);
}
Json tag(Json v, const char *type = "IntProperty") { return {{"type", {{"name", type}}}, {"value", v}}; }
Json opaque() {
    return {{"type", {{"name", "IntProperty"}}},
            {"opaque", {{"bytes", 4}, {"sha256", std::string(64, 'a')}, {"reason", "opaque"}}}};
}
} // namespace
int main() {
    try {
        Json a = {{"Nested", tag(Json{{"Value", tag(42)}, {"Known", tag(1)}}, "StructProperty")}}, b = a;
        b["Nested"]["value"]["Value"] = opaque();
        b["Nested"]["value"]["Known"]["value"] = 2;
        PropertyTableOptions options;
        options.coverage.after_unavailable = {"/Nested/value/Value/value"};
        options.coverage.comparison_unavailable = options.coverage.after_unavailable;
        auto table = property_table(&a, &b, options);
        const auto &unknown = row(table, "/Nested/Value");
        check(unknown.change == ChangeKind::unknown && unknown.before == "42" &&
                  unknown.after == "Unavailable" && unknown.before_available && !unknown.after_available,
              "Unread scalar preserves its known peer without a removal marker");
        check(unknown.source_path == "/Nested/value/Value" &&
                  row(table, "/Nested/Known").change == ChangeKind::modified,
              "Logical row identities remain compatible while coverage uses actual stored pointers");
        PropertyPageOptions page_options;
        page_options.coverage = options.coverage;
        auto page = property_page(&a, &b, "/Nested/value", page_options);
        check(row(page.table, "/Nested/value/Value").change == ChangeKind::unknown &&
                  row(page.table, "/Nested/value/Known").after == "2",
              "Paged unknown scopes retain independent known sibling changes");
        page = property_page(&a, &b, "/Nested/value/Value/value", page_options);
        check(page.available && page.table.rows.size() == 1 &&
                  page.table.rows[0].change == ChangeKind::unknown && page.table.rows[0].before == "42",
              "A missing opaque value remains an unavailable scope rather than a missing property");
        b = a;
        b["Nested"]["value"]["Value"]["resolvedType"] = {{"name", "IntProperty"}};
        options = {};
        options.coverage.ignored = {"/Nested/value/Value/resolvedType"};
        table = property_table(&a, &b, options);
        check(row(table, "/Nested/Value").change == ChangeKind::unchanged &&
                  row(table, "/Nested/Value").context_only,
              "Resolved context does not mark a stored scalar as modified");
        page_options = {};
        page_options.coverage = options.coverage;
        page_options.serialization_fields = true;
        page = property_page(&a, &b, "/Nested/value/Value", page_options);
        check(row(page.table, "/Nested/value/Value/resolvedType").context_only &&
                  row(page.table, "/Nested/value/Value/resolvedType").change == ChangeKind::unchanged,
              "Explicit serialization browsing distinguishes added context metadata");
        Json native = {{"type", {{"name", "IntProperty"}}},
                       {"opaque", {{"bytes", 4}, {"sha256", std::string(64, 'a')}}},
                       {"resolvedType", 1}},
             native_after = native;
        native_after["resolvedType"] = 2;
        options = {};
        options.coverage.raw = {""};
        table = property_table(&native, &native_after, options);
        check(row(table, "/resolvedType").change == ChangeKind::modified &&
                  row(table, "/resolvedType").before_available,
              "Explicit native JSON is not interpreted by field-name heuristics");
        Json callback = {{"Native", tag(Json{{"decoder", "custom/1"}, {"data", native}}, "StructProperty")}},
             callback_after = callback;
        callback_after["Native"]["value"]["data"] = native_after;
        table = property_table(&callback, &callback_after);
        check(row(table, "/Native/data/resolvedType").change == ChangeKind::modified &&
                  row(table, "/Native/data/resolvedType").source_path == "/Native/value/data/resolvedType",
              "Registered struct payload data keeps its explicit native namespace during expanded browsing");
        page = property_page(&callback, &callback_after, "/Native/value/data");
        check(row(page.table, "/Native/value/data/resolvedType").change == ChangeKind::modified,
              "Direct paged scopes preserve native data context through source wrapper ancestors");
        Json old_model = {{"Old", 1}}, new_model = {{"New", 2}};
        options = {};
        options.coverage.comparison_unavailable = {""};
        table = property_table(&old_model, &new_model, options);
        check(row(table, "/Old").change == ChangeKind::unknown && row(table, "/Old").before == "1" &&
                  row(table, "/Old").after == "Unavailable" && row(table, "/New").after == "2",
              "Incompatible schemas keep readable values while missing correspondence remains unknown");
        auto before = tag(7), after = tag(nullptr);
        after["serializationFlags"] = 32;
        table = property_table(&before, &after);
        check(row(table, "").change == ChangeKind::unknown && row(table, "").after == "Unavailable" &&
                  row(table, "/@tag/serializationFlags").change == ChangeKind::added,
              "Skipped serialization never becomes a known null and its stored tag flags remain visible");
        before = opaque();
        after = before;
        after["guid"] = "changed";
        table = property_table(&before, &after);
        check(row(table, "").change == ChangeKind::unknown &&
                  row(table, "/@tag/guid").change == ChangeKind::added,
              "Equal opaque bytes do not erase a changed stored tag identity");
        before["opaque"]["bytes"] = -1;
        after = before;
        table = property_table(&before, &after);
        check(row(table, "").change == ChangeKind::unknown,
              "Invalid opaque byte counts never prove stored equality");
        options = {};
        options.coverage.comparison_unavailable = {""};
        options.coverage.ignored = {""};
        table = property_table(&old_model, &new_model, options);
        check(!row(table, "").context_only && row(table, "").change == ChangeKind::unknown,
              "Unknown correspondence takes precedence over context-only presentation");
        options = {};
        options.coverage.before_unavailable = {"invalid"};
        try {
            property_table(&a, &b, options);
            throw std::runtime_error("Expected invalid coverage pointer");
        } catch (const Error &e) {
            check(e.code == "property-coverage-scope", "Invalid coverage masks fail explicitly");
        }
        options = {};
        options.control.cancelled = [] { return true; };
        try {
            property_table(&a, &b, options);
            throw std::runtime_error("Expected cancelled table");
        } catch (const Error &e) {
            check(e.code == "cancelled", "Expanded preparation honors cancellation before work");
        }
        check(change_name(ChangeKind::unknown) == std::string("Unknown"),
              "Unknown has a distinct English label");
        ChangeCounts counts;
        counts.add(ChangeKind::unknown);
        check(counts.unknown == 1 && counts.changed() == 0,
              "Unknown values do not inflate known change counts");
        std::cout << checks << " value coverage checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
