#include <cyuediff/visual_diff.hpp>
#include <iostream>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
const PropertyRow &row(const PropertyTable &table, const std::string &path) {
    for (const auto &r : table.rows)
        if (r.path == path)
            return r;
    throw std::runtime_error("Missing property row: " + path);
}
} // namespace
int main() {
    try {
        Json null = nullptr, one = 1, two = 2;
        check(classify_change(nullptr, &null) == ChangeKind::added, "Missing differs from JSON null");
        check(classify_change(&null, nullptr) == ChangeKind::removed, "Null can be removed");
        check(classify_change(&one, &two) == ChangeKind::modified, "Values modified");
        check(classify_change(nullptr, nullptr) == ChangeKind::unchanged, "Absent both");
        Json a = {{"nodes", {{"A", {{"value", 1}}}, {"B", {{"value", 2}}}}}, {"links", Json::object()}};
        auto b = a;
        b["links"]["A:B"] = {{"from", {{"node", "A"}, {"pin", "out"}}},
                             {"to", {{"node", "B"}, {"pin", "in"}}}};
        auto g = graph_changes(&a, &b);
        check(g.nodes.at("A") == ChangeKind::modified && g.nodes.at("B") == ChangeKind::modified,
              "New wire marks endpoints");
        check(g.link_counts.added == 1 && g.node_counts.modified == 2, "Wire and node counts");
        auto reverse = graph_changes(&b, &a);
        check(reverse.link_counts.removed == 1 && reverse.node_counts.modified == 2,
              "Removed wire marks both endpoints");
        b["nodes"]["C"] = {{"value", 3}};
        b["nodes"].erase("B");
        g = graph_changes(&a, &b);
        check(g.nodes.at("C") == ChangeKind::added && g.nodes.at("B") == ChangeKind::removed,
              "Stable node identities retained");
        check(graph_changes(&a, &a).node_counts.changed() == 0, "Self graph comparison");
        check(graph_changes(nullptr, &a).node_counts.added == 2, "Entire added graph");
        Json before = {{"Count", {{"type", {{"name", "IntProperty"}}}, {"value", 1}}},
                       {"nested", {{"a/b~c", nullptr}}},
                       {"items", Json::array({1, 2})},
                       {"Opaque", {{"opaque", {{"sha256", "abc"}}}}}};
        auto after = before;
        after["Count"]["value"] = 7;
        after["items"].push_back(3);
        after["nullable"] = nullptr;
        auto saved = before;
        auto t = property_table(&before, &after);
        check(row(t, "/Count").type == "IntProperty" && row(t, "/Count").before == "1" &&
                  row(t, "/Count").after == "7",
              "Typed tagged values");
        check(row(t, "/Count").change == ChangeKind::modified, "Typed value modified");
        check(row(t, "/nested/a~1b~0c").before == "null", "Unambiguous escaped path");
        check(row(t, "/nullable").change == ChangeKind::added && !row(t, "/nullable").before_present &&
                  row(t, "/nullable").after_present,
              "Absent versus null in table");
        check(row(t, "/items/2").change == ChangeKind::added, "Array tail addition");
        check(row(t, "/Opaque/opaque/sha256").after == "\"abc\"", "Opaque fingerprint visible");
        PropertyTableOptions limits;
        limits.max_rows = 3;
        auto limited = property_table(&before, &after, limits);
        check(limited.rows.size() == 3 && limited.truncated, "Row budget enforced");
        limits.max_rows = 20000;
        limits.max_depth = 0;
        check(property_table(&before, &after, limits).truncated, "Depth budget enforced");
        limits.max_depth = 48;
        limits.max_text_bytes = 3;
        Json unicode = "ééé";
        auto text = property_table(&unicode, &unicode, limits);
        check(text.rows[0].after == "\"é\"...", "UTF-8 truncation keeps valid boundary");
        Json tagged = {{"type", {{"name", "IntProperty"}}}, {"value", 1}, {"serializationFlags", 8}};
        auto changed_tag = tagged;
        changed_tag["serializationFlags"] = 40;
        auto tag_table = property_table(&tagged, &changed_tag);
        check(row(tag_table, "/@tag/serializationFlags").change == ChangeKind::modified,
              "Tag metadata changes remain visible");
        Json cooked = {{"/Script/Test.Asset.a/b~c",
                        {{"type", "IntProperty"}, {"value", 12}, {"serializedZero", false}}}};
        auto cooked_after = cooked;
        cooked_after["/Script/Test.Asset.a/b~c"]["value"] = 34;
        auto cooked_table = property_table(&cooked, &cooked_after);
        const auto &cooked_row = row(cooked_table, "/~1Script~1Test.Asset.a~1b~0c");
        check(cooked_row.type == "IntProperty" && cooked_row.before == "12" && cooked_row.after == "34",
              "Unversioned type strings unwrap values");
        check(cooked_row.label == "/Script/Test.Asset.a/b~c", "Display labels decode JSON pointer tokens");
        check(row(cooked_table, "/~1Script~1Test.Asset.a~1b~0c/@tag/serializedZero").after == "false",
              "Unversioned serialization metadata retained");
        Json literal = {{"a~1b", 1}};
        auto literal_table = property_table(&literal, &literal);
        check(row(literal_table, "/a~01b").label == "a~1b", "Display labels unescape once");
        check(before == saved, "Input snapshots remain immutable");
        limits.max_rows = 0;
        try {
            property_table(&before, &after, limits);
            throw std::runtime_error("Expected invalid limit");
        } catch (const Error &e) {
            check(e.code == "property-view-limit", "Invalid limits rejected");
        }
        std::cout << checks << " visual model checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
