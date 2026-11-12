#pragma once
#include <cyuediff/core.hpp>
#include <set>
#include <vector>
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
    const auto &display = value.get_ref<const std::string &>();
    const auto &base = name.at("base").get_ref<const std::string &>();
    const auto n = name.at("number").get<std::uint32_t>();
    if (!n)
        return display == base;
    const auto suffix = std::to_string(n - 1);
    return display.size() == base.size() + 1 + suffix.size() && display.compare(0, base.size(), base) == 0 &&
           display[base.size()] == '_' && display.compare(base.size() + 1, std::string::npos, suffix) == 0;
}
// Same acceptance as Json::json_pointer parsing, without allocating its tokens.
inline bool valid_json_pointer(const std::string &path) {
    if (path.empty())
        return true;
    if (path[0] != '/')
        return false;
    for (std::size_t i = 0; i < path.size(); ++i)
        if (path[i] == '~' && (i + 1 == path.size() || (path[i + 1] != '0' && path[i + 1] != '1')))
            return false;
    return true;
}
inline std::string unescape_pointer_token(const std::string &token) {
    std::string out;
    out.reserve(token.size());
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (token[i] == '~' && i + 1 < token.size()) {
            out += token[i + 1] == '1' ? '/' : '~';
            ++i;
        } else
            out += token[i];
    }
    return out;
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
        if (!valid_json_pointer(path))
            throw Error("name-values", "Invalid name-value location");
        try {
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
        // Components were validated by name_component_bytes; compare them in place.
        if (!name_display_matches(*na, *av) || !name_display_matches(*nb, *bv)) {
            unknown();
            return;
        }
        // Stored correspondence preserves display case; this is not a runtime key-equality policy.
        if (na->at("base") != nb->at("base") || na->at("number") != nb->at("number"))
            result.changes.push_back({{"op", "replace"},
                                      {"path", prefix + path},
                                      {"before", *av},
                                      {"after", *bv},
                                      {"beforeName", name_components(*na)},
                                      {"afterName", name_components(*nb)}});
    };
    // Both indexes are sorted maps: one merged walk finds peers. Visit order is unchanged:
    // every `a` path in order, then the paths present only in `b`, in order.
    std::vector<Json::const_iterator> only_b;
    auto j = bn.begin();
    for (auto i = an.begin(); i != an.end(); ++i) {
        while (j != bn.end() && j.key() < i.key())
            only_b.push_back(j++);
        if (j != bn.end() && j.key() == i.key()) {
            visit(i.key(), &*i, &*j);
            ++j;
        } else
            visit(i.key(), &*i, nullptr);
    }
    for (; j != bn.end(); ++j)
        only_b.push_back(j);
    for (const auto &k : only_b)
        visit(k.key(), nullptr, &*k);
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

// Resolves path[at..] (a JSON pointer) from `root` like Json::at(json_pointer), without building
// the pointer: a missing member or index yields null; a malformed array index is an error.
inline const Json *resolve_pointer(const Json &root, const std::string &path, std::size_t at) {
    const Json *current = &root;
    std::string token;
    while (at < path.size()) {
        const auto end = path.find('/', at + 1);
        const auto stop = end == std::string::npos ? path.size() : end;
        token.clear();
        for (auto i = at + 1; i < stop; ++i) {
            if (path[i] == '~' && i + 1 < stop) {
                token += path[i + 1] == '1' ? '/' : '~';
                ++i;
            } else
                token += path[i];
        }
        at = stop;
        if (current->is_object()) {
            const auto found = current->find(token);
            if (found == current->end())
                return nullptr;
            current = &*found;
        } else if (current->is_array()) {
            if (token == "-")
                return nullptr;
            // Same rules as nlohmann's array_index: a multi-character index starting with '0' or
            // with a non-digit is malformed; any other non-number is an unresolved (absent) index.
            if (token.size() > 1 && !(token[0] >= '1' && token[0] <= '9'))
                throw Error("name-values", "Invalid name-value location");
            if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos)
                return nullptr;
            std::size_t index = 0;
            for (const char c : token) {
                const auto digit = static_cast<std::size_t>(c - '0');
                if (index > (SIZE_MAX - digit) / 10)
                    return nullptr;
                index = index * 10 + digit;
            }
            if (index >= current->size())
                return nullptr;
            current = &(*current)[index];
        } else
            return nullptr;
    }
    return current;
}
inline const Json *source_name_value(const std::map<std::string, const ObjectExport *> &exports,
                                     const std::string &path) {
    if (path.rfind("/objects/", 0) != 0)
        throw Error("name-values", "Name-value index is outside source properties");
    const auto slash = path.find('/', 9);
    if (slash == std::string::npos || path.compare(slash, 11, "/properties") != 0 ||
        (path.size() > slash + 11 && path[slash + 11] != '/'))
        throw Error("name-values", "Invalid source name-value root");
    const auto object = unescape_pointer_token(path.substr(9, slash - 9));
    const auto found = exports.find(object);
    if (found == exports.end())
        return nullptr;
    return resolve_pointer(found->second->properties, path, slash + 11);
}
} // namespace cyuediff::detail
