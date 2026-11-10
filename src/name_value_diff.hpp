#pragma once
#include <cyuediff/core.hpp>
#include <set>
namespace cyuediff::detail {
inline const Json &name_value_entries(const Json &index, const char *root) {
    static const Json empty = Json::object();
    if (index.is_null())
        return empty;
    if (!index.is_object() || index.value("schema", "") != "cyuediff.name-values/1" ||
        index.value("root", "") != root ||
        (index.value("status", "") != "complete" && index.value("status", "") != "incomplete") ||
        !index.contains("names") || !index.at("names").is_object())
        throw Error("name-values", "Invalid typed name-value index");
    return index.at("names");
}
inline std::size_t name_component_bytes(const Json &node) {
    if (!node.is_object() || !node.contains("base") || !node.at("base").is_string() ||
        !node.contains("number") || !node.at("number").is_number_integer() ||
        (!node.at("number").is_number_unsigned() && node.at("number").get<std::int64_t>() < 0) ||
        node.at("number").get<std::uint64_t>() > INT32_MAX)
        throw Error("name-values", "Invalid stored FName components");
    return node.at("base").get_ref<const std::string &>().size();
}
inline Json name_components(const Json &node) {
    name_component_bytes(node);
    return {{"base", node.at("base")}, {"number", node.at("number")}};
}
inline bool name_display_matches(const Json &name, const Json &value) {
    if (!value.is_string())
        return false;
    auto display = name.at("base").get<std::string>();
    const auto n = name.at("number").get<std::uint32_t>();
    if (n)
        display += "_" + std::to_string(n - 1);
    return value == display;
}
struct NameValueDiff {
    Json changes = Json::array(), unavailable = Json::array();
};
template <class LookupA, class LookupB>
NameValueDiff compare_name_values(const Json &a, const Json &b, LookupA get_a, LookupB get_b,
                                  const char *root, const std::string &prefix,
                                  const OperationControl &control = {}) {
    const auto &an = name_value_entries(a, root), &bn = name_value_entries(b, root);
    NameValueDiff result;
    std::size_t nodes = 0, bytes = 0;
    auto visit = [&](const std::string &path, const Json *na, const Json *nb) {
        control.check();
        if (++nodes > 200000 || path.size() > 16 * 1024 * 1024 - bytes)
            throw Error("name-values-limit", "Name correspondence budget exceeded");
        bytes += path.size();
        const Json *av = nullptr, *bv = nullptr;
        try {
            Json::json_pointer valid(path);
            (void)valid;
            av = get_a(path);
            bv = get_b(path);
        } catch (const Json::exception &) {
            throw Error("name-values", "Invalid name-value location");
        }
        // Added/removed enclosing fields are already represented by the ordinary value traversal.
        if (!av || !bv)
            return;
        auto unknown = [&] {
            result.unavailable.push_back(
                {{"path", prefix + path}, {"reason", "stored-name-components-unavailable"}});
        };
        if (!na || !nb) {
            unknown();
            return;
        }
        const auto cost = name_component_bytes(*na) + name_component_bytes(*nb);
        if (cost > 16 * 1024 * 1024 - bytes)
            throw Error("name-values-limit", "Name component budget exceeded");
        bytes += cost;
        auto x = name_components(*na), y = name_components(*nb);
        if (!name_display_matches(x, *av) || !name_display_matches(y, *bv)) {
            unknown();
            return;
        }
        // Stored correspondence preserves display case; this is not a runtime key-equality policy.
        if (x != y)
            result.changes.push_back({{"op", "replace"},
                                      {"path", prefix + path},
                                      {"before", *av},
                                      {"after", *bv},
                                      {"beforeName", std::move(x)},
                                      {"afterName", std::move(y)}});
    };
    for (auto i = an.begin(); i != an.end(); ++i) {
        const auto peer = bn.find(i.key());
        visit(i.key(), &*i, peer == bn.end() ? nullptr : &*peer);
    }
    for (auto i = bn.begin(); i != bn.end(); ++i)
        if (!an.contains(i.key()))
            visit(i.key(), nullptr, &*i);
    return result;
}
inline void merge_name_changes(Json &changes, const Json &names) {
    if (names.empty())
        return;
    std::map<std::string, std::size_t> positions;
    for (const auto &row : names)
        positions.emplace(row.at("path").get<std::string>(), SIZE_MAX);
    for (std::size_t i = 0; i < changes.size(); ++i) {
        auto found = positions.find(changes[i].at("path").get<std::string>());
        if (found != positions.end())
            found->second = i;
    }
    for (const auto &row : names) {
        const auto index = positions.at(row.at("path").get<std::string>());
        if (index == SIZE_MAX)
            changes.push_back(row);
        else {
            changes[index]["beforeName"] = row.at("beforeName");
            changes[index]["afterName"] = row.at("afterName");
        }
    }
}

inline const Json *source_name_value(const std::map<std::string, const ObjectExport *> &exports,
                                     const std::string &path) {
    if (path.rfind("/objects/", 0) != 0)
        throw Error("name-values", "Name-value index is outside source properties");
    const auto slash = path.find('/', 9);
    if (slash == std::string::npos || path.compare(slash, 11, "/properties") != 0 ||
        (path.size() > slash + 11 && path[slash + 11] != '/'))
        throw Error("name-values", "Invalid source name-value root");
    const auto object = Json::json_pointer("/" + path.substr(9, slash - 9)).back();
    const auto found = exports.find(object);
    if (found == exports.end())
        return nullptr;
    try {
        return &found->second->properties.at(Json::json_pointer(path.substr(slash + 11)));
    } catch (const Json::out_of_range &) {
        return nullptr;
    }
}
} // namespace cyuediff::detail
