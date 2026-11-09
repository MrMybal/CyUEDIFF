#include <cyuediff/property_page.hpp>
#include <cyuediff/view_state.hpp>
#include <iostream>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool x, const char *message) {
    ++checks;
    if (!x)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, "Expected error code");
        return;
    }
    throw std::runtime_error("Expected rejection");
}
} // namespace
int main() {
    try {
        Json before = {
            {"a/b~c", {{"type", "ArrayProperty"}, {"value", Json::array()}, {"serializedZero", false}}},
            {"z", 7}};
        for (int i = 0; i < 40001; ++i)
            before["a/b~c"]["value"].push_back({{"particle", i}, {"length", 1.25}});
        auto after = before;
        after["a/b~c"]["value"][40000]["length"] = 2.5;
        auto original = before;
        const auto root = property_page(&before, &after);
        check(root.table.rows.size() == 3 && root.total_children == 2 && !root.table.truncated,
              "Only direct children at root");
        check(root.table.rows[1].label == "a/b~c" && root.table.rows[1].path == "/a~1b~0c" &&
                  root.expandable[1],
              "Exact escaped raw target");
        check(root.table.rows[1].type == "ArrayProperty" && root.table.rows[1].change == ChangeKind::modified,
              "Typed summary preserves deep difference");
        PropertyPageOptions options;
        options.first = 40000 / 128 * 128;
        auto page = property_page(&before, &after, "/a~1b~0c", options);
        check(page.unwrapped && page.array && page.total_children == 40001 && page.first == 39936 &&
                  page.table.rows.size() == 66,
              "Last array page is reachable");
        check(!page.table.truncated && page.table.rows.back().path == "/a~1b~0c/value/40000" &&
                  page.table.rows.back().change == ChangeKind::modified,
              "Pagination is not safety truncation");
        auto value = property_page(&before, &after, page.table.rows.back().path);
        check(value.total_children == 2 && value.table.rows[1].label == "length" &&
                  value.table.rows[1].before == "1.25" && value.table.rows[1].after == "2.5",
              "Late child exact named values");
        options.first = 2'000'000;
        check(property_page(&before, &after, "/a~1b~0c", options).first == 39936,
              "Stale offset clamps to final page");
        options.serialization_fields = true;
        options.first = 0;
        page = property_page(&before, &after, "/a~1b~0c", options);
        check(!page.unwrapped && !page.array && page.total_children == 3 &&
                  page.table.rows[1].label == "serializedZero",
              "Tag fields remain independently accessible");
        check(page.table.rows[3].path == "/a~1b~0c/value" && page.expandable[3],
              "Raw stored value can be opened");
        auto added = property_page(nullptr, &after, "/a~1b~0c/value/40000");
        check(added.table.rows[1].change == ChangeKind::added && !added.table.rows[1].before_present,
              "Known absent subtree additions");
        auto removed = property_page(&before, nullptr, "/a~1b~0c/value/40000");
        check(removed.table.rows[1].change == ChangeKind::removed, "Known absent subtree removals");
        Json n = nullptr;
        auto null = property_page(&n, nullptr);
        check(null.available && null.total_children == 0 && null.table.rows[0].before_present,
              "Present null remains available");
        check(!property_page(&before, &after, "/missing").available &&
                  property_page(nullptr, nullptr).table.rows.empty(),
              "Missing scopes are unavailable");
        check(!property_page(&before, &after, "/a~1b~0c/value/040000").available, "No array index aliases");
        Json literal = {{"@tag", {{"value", 9}}}, {"a~1b", 2}, {"", 4}};
        auto literals = property_page(&literal, &literal);
        check(literals.total_children == 3 &&
                  property_page(&literal, &literal, "/").table.rows[0].before == "4",
              "Empty field name differs from root");
        check(property_page(&literal, &literal, "/a~01b").table.rows[0].label == "a~1b",
              "Escapes decode exactly once");
        check(property_page(&literal, &literal, "/@tag").total_children == 1,
              "Stored @tag is not synthetic metadata");
        Json number = 1, floating = 1.0;
        check(property_page(&number, &floating).table.rows[0].change == ChangeKind::unchanged,
              "Numeric classification agrees with existing table");
        Json array = Json::array({1, 2});
        auto replaced = property_page(&literal, &array);
        check(replaced.total_children == 0 && replaced.table.rows[0].change == ChangeKind::modified,
              "Incompatible containers are a whole-value replacement");
        Json left = Json::object(), right = Json::object();
        for (int i = 0; i < 35000; ++i)
            (i % 2 ? left : right)["key" + std::to_string(100000 + i)] = i;
        options = {};
        options.first = 34944;
        auto objects = property_page(&left, &right, "", options);
        check(objects.total_children == 35000 && objects.table.rows.back().path == "/key134999",
              "Sorted union object pagination reaches last field");
        check(objects.table.rows.back().change == ChangeKind::removed,
              "Object page preserves side membership");
        for (const auto &bad : {"bad", "/~", "/~2"})
            rejects([&] { property_page(&before, &after, bad); }, "property-page-scope");
        options = {};
        options.page_size = 0;
        rejects([&] { property_page(&before, &after, "", options); }, "property-page-limit");
        options.page_size = 513;
        rejects([&] { property_page(&before, &after, "", options); }, "property-page-limit");
        options = {};
        options.max_compare_nodes = 1;
        rejects([&] { property_page(&before, &after, "", options); }, "property-page-limit");
        options = {};
        options.control.cancelled = [] { return true; };
        rejects([&] { property_page(&before, &after, "", options); }, "cancelled");
        int callbacks = 0;
        options.control.cancelled = [&] { return callbacks > 0; };
        options.control.progress = [&](const OperationProgress &) { ++callbacks; };
        rejects([&] { property_page(&before, &after, "", options); }, "cancelled");
        check(callbacks == 1, "Comparison responds to cancellation within page preparation");
        auto state = Json{{"scope", "/a~1b~0c/value/40000"},
                          {"first", 0},
                          {"serializationFields", false},
                          {"properties", Json::object()}};
        view_state::validate(state, "property-browser");
        for (const auto &bad : {Json("/~2"), Json(1)}) {
            state["scope"] = bad;
            rejects([&] { view_state::validate(state, "property-browser"); }, "view-state");
        }
        check(before == original && after["a/b~c"]["value"][40000]["length"] == 2.5,
              "Snapshots remain unchanged");
        std::cout << checks << " property page checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
