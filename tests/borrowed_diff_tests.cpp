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
