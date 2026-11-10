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
const PropertyRow &row(const PropertyPage &page, const std::string &path) {
    for (const auto &r : page.table.rows)
        if (r.path == path)
            return r;
    throw std::runtime_error("Missing metadata row " + path);
}
Json opaque() {
    Json t = {{"name", "StructProperty"}, {"structGuid", std::string(32, '1')}};
    return {{"type", t},
            {"guid", std::string(32, '2')},
            {"opaque", {{"bytes", 8}, {"sha256", std::string(64, 'a')}}}};
}
} // namespace
int main() {
    try {
        Json a = {{"Identity", opaque()}}, b = a;
        b["Identity"].erase("opaque");
        b["Identity"]["value"] = Json::object();
        b["Identity"]["resolvedType"] = b["Identity"]["type"];
        b["Identity"]["resolvedType"]["schema"] = "/Game/Original.IntPoint";
        const auto saved = a, saved_b = b;
        PropertyPageOptions o;
        o.serialization_fields = true;
        auto page = property_page(&a, &b, "/Identity", o);
        check(row(page, "/Identity/type").change == ChangeKind::unchanged &&
                  row(page, "/Identity/type").before_available && row(page, "/Identity/type").after_available,
              "An unread value does not erase the stored type");
        check(row(page, "/Identity/guid").change == ChangeKind::unchanged,
              "Member GUID remains available independently of the value");
        check(row(page, "/Identity/value").change == ChangeKind::unknown &&
                  !row(page, "/Identity/value").before_available &&
                  row(page, "/Identity/value").after_available,
              "Empty decoded peer does not turn an unread value into an addition");
        check(row(page, "/Identity").change == ChangeKind::unknown,
              "Metadata inspection retains the unread value summary");
        auto guid = property_page(&a, &b, "/Identity/type/structGuid", o);
        check(guid.table.rows[0].before_available && guid.table.rows[0].after_available &&
                  guid.table.rows[0].change == ChangeKind::unchanged,
              "Exact stored struct GUID is inspectable");
        auto value = property_page(&a, &b, "/Identity/value", o);
        check(value.table.rows[0].change == ChangeKind::unknown && !value.table.rows[0].before_available,
              "Directly scoped absent unread value stays unknown without external masks");
        auto missing = property_page(&a, &b, "/Identity/value/notYetKnown", o);
        check(missing.available && missing.table.rows[0].change == ChangeKind::unknown &&
                  !missing.table.rows[0].before_available,
              "An absent descendant below an unread ancestor remains an inspectable unknown scope");
        auto reverse = property_page(&b, &a, "/Identity", o);
        check(row(reverse, "/Identity/value").change == ChangeKind::unknown &&
                  !row(reverse, "/Identity/value").after_available,
              "Reverse unread value is not removed");
        o.coverage.before_unavailable = {"/Identity/value"};
        o.coverage.comparison_unavailable = {"/Identity/value"};
        o.coverage.ignored = {"/Identity/resolvedType", "/Identity/opaque"};
        page = property_page(&a, &b, "/Identity", o);
        check(row(page, "/Identity/type").change == ChangeKind::unchanged &&
                  row(page, "/Identity/type").before_available,
              "Value-only side masks retain actual tags");
        check(row(page, "/Identity/resolvedType").context_only && row(page, "/Identity/opaque").context_only,
              "Reader annotations remain context rather than stored additions/removals");
        o.coverage.before_unavailable = {"/Identity"};
        page = property_page(&a, &b, "/Identity", o);
        check(!row(page, "/Identity/type").before_available &&
                  row(page, "/Identity/type").change == ChangeKind::unknown,
              "Whole unavailable export/parent masks override stale stored metadata");
        o = {};
        o.serialization_fields = true;
        auto absent = property_page(nullptr, &b, "/Identity", o);
        check(row(absent, "/Identity/type").change == ChangeKind::added &&
                  row(absent, "/Identity/value").change == ChangeKind::added,
              "Known missing peer remains genuine absence");
        auto skipped = b;
        skipped["Identity"]["serializationFlags"] = 32;
        skipped["Identity"]["value"] = {{"secretStaleChild", 1}};
        page = property_page(&skipped, &b, "/Identity", o);
        check(row(page, "/Identity/type").before_available && !row(page, "/Identity/value").before_available,
              "Skipped tags retain metadata without stale values");
        value = property_page(&skipped, &b, "/Identity/value/secretStaleChild", o);
        check(value.table.rows[0].change == ChangeKind::unknown && !value.table.rows[0].before_available,
              "Unread skipped ancestor masks its stale descendants");
        auto raw = Json{{"decoder", "original.raw/1"}, {"data", opaque()}};
        auto raw_after = raw;
        raw_after["data"]["opaque"]["bytes"] = 9;
        page = property_page(&raw, &raw_after, "/data/opaque/bytes", o);
        check(page.table.rows[0].before_available && page.table.rows[0].change == ChangeKind::modified,
              "Native raw members named type/opaque retain ordinary data semantics");
        o.coverage.raw = {""};
        page = property_page(&a, &b, "/Identity/value", o);
        check(page.table.rows[0].change == ChangeKind::added,
              "Explicit raw host JSON is not reinterpreted as source tags");
        Json null = nullptr;
        check(property_page(&null, &null, "", o).table.rows[0].before_available,
              "Present JSON null remains readable");
        auto annotated = b;
        annotated["Identity"]["readFormats"] = {{"", "tagged"}};
        PropertyTableOptions table_options;
        table_options.coverage.ignored = {"/Identity/readFormats"};
        const auto table = property_table(&b, &annotated, table_options);
        bool exposed = false;
        for (const auto &r : table.rows)
            exposed = exposed || r.source_path == "/Identity/readFormats";
        check(!exposed,
              "Default property tables keep reader provenance in explicit serialization inspection");
        Json raw_formats = {{"decoder", "host.raw/1"}, {"data", {{"readFormats", 1}}}};
        auto changed_formats = raw_formats;
        changed_formats["data"]["readFormats"] = 2;
        const auto raw_table = property_table(&raw_formats, &changed_formats);
        bool real_change = false;
        for (const auto &r : raw_table.rows)
            real_change =
                real_change || (r.source_path == "/data/readFormats" && r.change == ChangeKind::modified);
        check(real_change, "Native data named readFormats stays ordinary comparable data");
        check(a == saved && b == saved_b, "Metadata inspection does not mutate snapshots");
        std::cout << checks << " property metadata checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
