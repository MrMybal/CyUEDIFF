#include <cyuediff/core.hpp>
#include <iostream>
#include <algorithm>
using namespace cyuediff;
namespace {
unsigned checks = 0;
void check(bool v, const char *why) {
    ++checks;
    if (!v)
        throw std::runtime_error(why);
}
Json prop(Json value) { return {{"type", {{"name", "IntProperty"}}}, {"value", std::move(value)}}; }
ObjectExport object(const std::string &path, int value) {
    ObjectExport e;
    e.path = path;
    e.coverage = "properties";
    e.class_name = "DataAsset";
    e.properties = {{"a~/b", prop(value)},
                    {"Array", {{"type", {{"name", "ArrayProperty"}}}, {"value", Json::array({1, 2, 3})}}}};
    e.native_coverage = "extension";
    e.native_data = {{"decoder", "same/1"}, {"data", {{"value", value}}}};
    return e;
}
// Independent reference uses the existing public materialized snapshot and RFC6902.
Json expected(const Package &a, const Package &b) {
    auto x = a.semantic_snapshot(), y = b.semantic_snapshot();
    Json out = Json::array();
    for (const auto &op : Json::diff(x, y)) {
        const auto path = op.at("path").get<std::string>();
        Json row = {{"op", op.at("op")}, {"path", path}};
        if (op.at("op") != "add")
            row["before"] = x.at(Json::json_pointer(path));
        if (op.at("op") != "remove")
            row["after"] = op.at("value");
        out.push_back(std::move(row));
    }
    return out;
}
Json ordered(Json rows) {
    std::sort(rows.begin(), rows.end(), [](const Json &a, const Json &b) { return a.dump() < b.dump(); });
    return rows;
}
} // namespace
int main() {
    try {
        Package a;
        a.exports = {object("Z/~", 1), object("Keep", 3), object("Gone", 8)};
        a.imports = {{"Class", "/Script/CoreUObject", "Class", ""}};
        a.exports[0].super_index = -1;
        TextureSource t;
        t.object_path = "Tex/~";
        t.width = 64;
        t.height = 32;
        t.format = "TSF_BGRA8";
        a.textures.push_back(t);
        auto b = a;
        b.exports[0].properties["a~/b"]["value"] = 2;
        b.exports[0].native_data["data"]["value"] = 2;
        b.exports[0].flags = 8;
        b.exports[0].category = "changed";
        b.exports[0].class_name = "OtherAsset";
        b.exports[0].properties["Array"]["value"][1] = 4;
        b.exports[0].template_index = -1;
        b.exports[0].super_index = 0;
        b.exports.erase(b.exports.begin() + 2);
        b.exports.push_back(object("Added", 9));
        b.textures[0].width = 128;
        auto before = a.to_json(), after = b.to_json();
        auto result = compare(a, b);
        check(
            result.unavailable_scopes.empty() && ordered(result.changes) == ordered(expected(a, b)),
            "Borrowed export metadata/data/lifecycle and texture projections match public snapshot changes");
        check(a.to_json() == before && b.to_json() == after,
              "Borrowed comparison never mutates either package");
        check(result.changes[0]["path"].get<std::string>().find("/objects/Gone") == 0,
              "Removed source objects preserve sorted traversal order");
        bool escaped = false;
        for (const auto &row : result.changes)
            escaped = escaped || row.at("path") == "/objects/Z~1~0/properties/a~0~1b/value";
        check(escaped, "Escaped object and property identities remain exact");
        auto reordered = b;
        std::reverse(reordered.exports.begin(), reordered.exports.end());
        check(compare(a, reordered).to_json() == result.to_json(),
              "Source export table reordering does not alter comparison facts");
        auto owned = []() {
            Package x, y;
            x.exports.push_back(object("Owned", 1));
            y = x;
            y.exports[0].properties["a~/b"]["value"] = 2;
            return compare(x, y);
        }();
        check(owned.changes.size() == 1 && owned.changes[0]["before"] == 1 && owned.changes[0]["after"] == 2,
              "Returned changes own their values after all source packages are destroyed");
        for (int seed = 0; seed < 24; ++seed) {
            auto x = a, y = a;
            y.exports[seed % 3].properties["a~/b"]["value"] = seed + 100;
            y.exports[seed % 3].native_data["data"]["value"] = seed + 200;
            std::rotate(y.exports.begin(), y.exports.begin() + seed % 3, y.exports.end());
            check(ordered(compare(x, y).changes) == ordered(expected(x, y)),
                  "Borrowed changes match materialized snapshot across independent changed objects and "
                  "reordering");
        }
        Package x, y;
        x.exports = {object("Exact", 1)};
        y = x;
        x.exports[0].native_data["data"]["value"] = std::int64_t(9007199254740993LL);
        y.exports[0].native_data["data"]["value"] = 9007199254740992.0;
        check(compare(x, y).changes.size() == 1,
              "Generic source diff detects mixed numeric rounding loss inside an explicit native model");
        {
            // Arrays of different lengths report insertions/removals, not a whole-array replacement.
            const std::string array = "/objects/Keep/properties/Array/value";
            auto changes_for = [&](const Json &before, const Json &after) {
                Package p, q;
                p.exports = {object("Keep", 3)};
                q = p;
                p.exports[0].properties["Array"]["value"] = before;
                q.exports[0].properties["Array"]["value"] = after;
                return compare(p, q).changes;
            };
            auto front = changes_for({1, 2, 3, 4, 5, 6}, {0, 1, 2, 3, 4, 5, 6});
            check(front == Json::array({{{"op", "add"}, {"path", array + "/0"}, {"after", 0}}}),
                  "Inserting at the front of an array is one addition");
            auto middle = changes_for({1, 2, 3, 4, 5}, {1, 2, 4, 5});
            check(middle == Json::array({{{"op", "remove"}, {"path", array + "/2"}, {"before", 3}}}),
                  "Removing inside an array is one removal at its before index");
            auto mixed = changes_for({1, 2, 3}, {1, 9, 8, 3});
            check(mixed == Json::array({{{"op", "replace"}, {"path", array + "/1"}, {"before", 2}, {"after", 9}},
                                        {{"op", "add"}, {"path", array + "/2"}, {"after", 8}}}),
                  "Changed and inserted middle elements are paired, then added");
            auto structs = changes_for(Json::array({{{"x", 1}}, {{"x", 2}}}),
                                       Json::array({{{"x", 1}}, {{"x", 5}}, {{"x", 2}}}));
            check(structs == Json::array({{{"op", "add"}, {"path", array + "/1"}, {"after", {{"x", 5}}}}}),
                  "Struct elements align by value");
            check(changes_for(Json::array(), {1}) ==
                      Json::array({{{"op", "add"}, {"path", array + "/0"}, {"after", 1}}}),
                  "Empty arrays gain elements by addition");

            // Stored typed names correspond by identical index: keep index pairing there.
            Package p, q;
            p.exports = {object("Keep", 3)};
            q = p;
            p.exports[0].properties["Array"]["value"] = {"A", "B"};
            q.exports[0].properties["Array"]["value"] = {"N", "A", "B"};
            auto names = [&](const std::vector<std::string> &values) {
                Json entries = Json::object();
                for (std::size_t i = 0; i < values.size(); ++i)
                    entries[array + "/" + std::to_string(i)] = {{"base", values[i]}, {"number", 0}};
                return Json{{"schema", "cyuediff.name-values/1"},
                            {"root", "semantic-snapshot"},
                            {"status", "complete"},
                            {"names", entries}};
            };
            p.name_values = names({"A", "B"});
            q.name_values = names({"N", "A", "B"});
            const auto named = compare(p, q).changes;
            check(named.size() == 3 && named[0].at("path") == array + "/0" && named[0].at("op") == "replace" &&
                      named[0].contains("afterName") && named[2].at("op") == "add" &&
                      named[2].at("path") == array + "/2",
                  "Arrays holding stored names keep index correspondence with their name changes");

            // Name locations resolve like JSON pointers: escaped tokens, absent members ignored,
            // malformed locations rejected.
            Package r, s;
            r.exports = {object("Z/~", 3)};
            s = r;
            r.exports[0].properties["Array"]["value"] = {"A_1"};
            s.exports[0].properties["Array"]["value"] = {"A_1"};
            const std::string escaped = "/objects/Z~1~0/properties/Array/value";
            auto index_with = [](Json entries) {
                return Json{{"schema", "cyuediff.name-values/1"},
                            {"root", "semantic-snapshot"},
                            {"status", "complete"},
                            {"names", std::move(entries)}};
            };
            r.name_values = index_with({{escaped + "/0", {{"base", "A"}, {"number", 2}}},
                                        {escaped + "/7", {{"base", "A"}, {"number", 2}}},
                                        {"/objects/Z~1~0/properties/Missing/value", {{"base", "A"}, {"number", 0}}}});
            s.name_values = index_with({{escaped + "/0", {{"base", "A_1"}, {"number", 0}}},
                                        {escaped + "/7", {{"base", "A"}, {"number", 2}}}});
            const auto resolved = compare(r, s).changes;
            check(resolved.size() == 1 && resolved[0].at("path") == escaped + "/0" &&
                      resolved[0].at("beforeName") == Json({{"base", "A"}, {"number", 2}}),
                  "Escaped name locations resolve; absent members and indices are ignored");
            auto rejects_location = [&](const std::string &location) {
                auto bad = r;
                bad.name_values = index_with({{location, {{"base", "A"}, {"number", 2}}}});
                try {
                    (void)compare(bad, s);
                } catch (const Error &e) {
                    return e.code == "name-values";
                }
                return false;
            };
            check(rejects_location(escaped + "/01"), "Leading-zero array indices are invalid name locations");
            check(rejects_location(escaped + "/+1"), "Signed array indices are invalid name locations");
            {
                auto unresolved = r;
                unresolved.name_values = index_with({{escaped + "/x", {{"base", "A"}, {"number", 2}}},
                                                     {escaped + "/1x", {{"base", "A"}, {"number", 2}}}});
                check(compare(unresolved, s).changes.empty(),
                      "Other non-numeric array indices are unresolved locations, not errors");
            }
            check(rejects_location("/objects/Z~2/properties/Array/value/0"),
                  "Invalid pointer escapes are invalid name locations");
        }
        OperationControl cancelled;
        cancelled.cancelled = [] { return true; };
        try {
            compare(a, b, cancelled);
            throw std::runtime_error("Expected cancellation");
        } catch (const Error &e) {
            check(e.code == "cancelled", "Source borrowing preserves cancellation before work");
        }
        std::cout << checks << " borrowed source diff checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
