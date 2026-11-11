#include <cyuediff/defaults.hpp>
#include "effective_defaults_budget.hpp"
#include "json_equality.hpp"
namespace cyuediff {
namespace {
struct Compare : detail::EffectiveBudget {
    const EffectiveDefaults &before;
    const EffectiveDefaults &after;
    EffectiveDefaultsComparison result;
    std::map<std::string, Json> unknowns;
    Compare(const EffectiveDefaults &a, const EffectiveDefaults &b, const EffectiveDefaultsOptions &o)
        : EffectiveBudget(o), before(a), after(b) {}
    void unknown(const std::string &path, const char *reason, bool a = false, bool b = false) {
        spend(1, path.size() + 128);
        auto it = unknowns.find(path);
        if (it == unknowns.end())
            unknowns[path] = {
                {"path", path}, {"reason", reason}, {"beforeUnavailable", a}, {"afterUnavailable", b}};
        else {
            it->second["beforeUnavailable"] = it->second["beforeUnavailable"].get<bool>() || a;
            it->second["afterUnavailable"] = it->second["afterUnavailable"].get<bool>() || b;
        }
    }
    bool uncertain(const std::string &path) {
        if (unknowns.count(path))
            return true;
        // Search descendants from "path/": siblings such as "path-x" or "path.x" sort between
        // "path" and "path/..." and would otherwise hide an unknown descendant scope.
        const auto prefix = path + '/';
        auto it = unknowns.lower_bound(prefix);
        if (it != unknowns.end() && it->first.compare(0, prefix.size(), prefix) == 0)
            return true;
        spend(0, path.size());
        auto parent = path;
        while (true) {
            if (unknowns.count(parent))
                return true;
            const auto slash = parent.rfind('/');
            if (slash == std::string::npos)
                return false;
            parent.resize(slash);
            spend();
        }
    }
    bool covered(const std::string &path) {
        spend(0, path.size());
        auto parent = path;
        while (true) {
            if (unknowns.count(parent))
                return true;
            const auto slash = parent.rfind('/');
            if (slash == std::string::npos)
                return false;
            parent.resize(slash);
            spend();
        }
    }
    void seed(const EffectiveDefaults &model, bool left) {
        for (const auto &row : model.unavailable) {
            inspect(row);
            unknown(row.at("path").get<std::string>(), "effective-value-unavailable", left, !left);
        }
        if (!model.complete && model.unavailable.empty())
            unknown("", "effective-model-incomplete", left, !left);
    }
    void change(const Json *a, const Json *b, const std::string &path) {
        spend(1, path.size() + 64);
        Json row = {{"op", a ? (b ? "replace" : "remove") : "add"}, {"path", path}};
        if (a)
            row["before"] = copy(*a);
        if (b)
            row["after"] = copy(*b);
        result.changes.push_back(std::move(row));
    }
    bool equal(const Json &a, const Json &b) {
        inspect(a);
        inspect(b);
        return detail::json_equal(a, b, options.control);
    }
    bool same_key_contract() const {
        return before.provenance.value("keyEquality", "") == "ue5.3-windows-ascii-keys/1" &&
               after.provenance.value("keyEquality", "") == "ue5.3-windows-ascii-keys/1";
    }
    void raw(const Json *a, const Json *b, const std::string &path, unsigned d) {
        depth(d);
        if (covered(path))
            return;
        if (!a || !b) {
            if (uncertain(path))
                unknown(path, "effective-added-or-removed-value-unavailable", a != nullptr, b != nullptr);
            else
                change(a, b, path);
            return;
        }
        if (a->is_object() && b->is_object()) {
            for (auto it = a->begin(); it != a->end(); ++it) {
                const auto peer = b->find(it.key());
                raw(&it.value(), peer == b->end() ? nullptr : &*peer, path_child(path, it.key()), d + 1);
            }
            for (auto it = b->begin(); it != b->end(); ++it)
                if (!a->contains(it.key()))
                    raw(nullptr, &it.value(), path_child(path, it.key()), d + 1);
        } else if (a->is_array() && b->is_array()) {
            for (std::size_t i = 0; i < std::max(a->size(), b->size()); ++i)
                raw(i < a->size() ? &(*a)[i] : nullptr, i < b->size() ? &(*b)[i] : nullptr,
                    path_child(path, std::to_string(i)), d + 1);
        } else if (!equal(*a, *b))
            change(a, b, path);
    }
    void fields(const Json &a, const Json &b, const std::string &path, unsigned d) {
        depth(d);
        for (auto it = a.begin(); it != a.end(); ++it) {
            auto peer = b.find(it.key());
            field(&it.value(), peer == b.end() ? nullptr : &*peer, path_child(path, it.key()), d + 1);
        }
        for (auto it = b.begin(); it != b.end(); ++it)
            if (!a.contains(it.key()))
                field(nullptr, &it.value(), path_child(path, it.key()), d + 1);
    }
    void field(const Json *a, const Json *b, const std::string &path, unsigned d) {
        depth(d);
        if (covered(path))
            return;
        if (!a || !b) {
            unknown(path, "effective-field-identity-unavailable", !a, !b);
            return;
        }
        const bool av = a->value("available", false) && a->contains("value"),
                   bv = b->value("available", false) && b->contains("value");
        if (!av || !bv) {
            unknown(path, "effective-value-unavailable", !av, !bv);
            return;
        }
        if (!equal(a->at("type"), b->at("type"))) {
            unknown(path, "effective-field-type-incompatible");
            return;
        }
        value(a->at("type"), &a->at("value"), &b->at("value"), path_child(path, "value"), d + 1);
    }
    void value(const Json &type, const Json *a, const Json *b, const std::string &path, unsigned d) {
        depth(d);
        if (covered(path))
            return;
        if (!a || !b) {
            raw(a, b, path, d + 1);
            return;
        }
        const auto kind = type.at("name").get<std::string>();
        if (kind == "NameProperty") {
            if (equal(*a, *b))
                return;
            if (!same_key_contract()) {
                unknown(path, "effective-name-equality-incompatible");
                return;
            }
            const auto an = a->at("number"), bn = b->at("number");
            if (an != bn) {
                change(a, b, path);
                return;
            }
            auto fold = [&](const Json &n, std::string &out) {
                const auto &s = n.at("base").get_ref<const std::string &>();
                spend(1, s.size());
                out = s;
                for (char &c : out) {
                    if (static_cast<unsigned char>(c) >= 128)
                        return false;
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c + ('a' - 'A'));
                }
                return true;
            };
            std::string x, y;
            if (!fold(*a, x) || !fold(*b, y)) {
                unknown(path, "effective-name-equality-outside-ascii-profile");
                return;
            }
            if (x != y)
                change(a, b, path);
            return;
        }
        if (kind == "StructProperty" && a->is_object() && b->is_object()) {
            fields(*a, *b, path, d + 1);
            return;
        }
        if (kind == "ArrayProperty") {
            const auto &element = type.at("arguments")[0];
            for (std::size_t i = 0; i < std::max(a->size(), b->size()); ++i)
                value(element, i < a->size() ? &(*a)[i] : nullptr, i < b->size() ? &(*b)[i] : nullptr,
                      path_child(path, std::to_string(i)), d + 1);
            return;
        }
        if (kind == "MapProperty" || kind == "SetProperty") {
            if (!same_key_contract()) {
                unknown(path, "effective-key-equality-incompatible");
                return;
            }
            const auto &ae = a->at("entries"), be = b->at("entries");
            const bool map = kind == "MapProperty";
            const auto entries_path = path_child(path, "entries");
            auto visit = [&](const std::string &id, const Json *av, const Json *bv) {
                const auto child = path_child(entries_path, id);
                if (!av || !bv) {
                    raw(av, bv, child, d + 1);
                    return;
                }
                const auto &key_type = type.at("arguments")[0];
                value(key_type, map ? &av->at("key") : av, map ? &bv->at("key") : bv,
                      map ? path_child(child, "key") : child, d + 1);
                if (map)
                    value(type.at("arguments")[1], &av->at("value"), &bv->at("value"),
                          path_child(child, "value"), d + 1);
            };
            for (auto it = ae.begin(); it != ae.end(); ++it) {
                auto peer = be.find(it.key());
                visit(it.key(), &it.value(), peer == be.end() ? nullptr : &*peer);
            }
            for (auto it = be.begin(); it != be.end(); ++it)
                if (!ae.contains(it.key()))
                    visit(it.key(), nullptr, &it.value());
            return;
        }
        raw(a, b, path, d + 1);
    }
};
} // namespace
std::string EffectiveDefaultsComparison::status() const {
    return !unavailable.empty() ? "unverified" : changes.empty() ? "identical" : "changed";
}
Json EffectiveDefaultsComparison::to_json() const {
    auto j = provenance;
    j["schema"] = "cyuediff.effective-default-diff/1";
    j["scope"] = "effective-reflected-properties";
    j["status"] = status();
    j["complete"] = unavailable.empty();
    j["changes"] = changes;
    j["unavailable"] = unavailable;
    return j;
}
EffectiveDefaultsComparison compare_effective_defaults(const EffectiveDefaults &before,
                                                       const EffectiveDefaults &after,
                                                       const EffectiveDefaultsOptions &options) {
    options.control.check();
    try {
        Compare compare(before, after, options);
        compare.inspect(before.provenance);
        compare.inspect(after.provenance);
        compare.seed(before, true);
        compare.seed(after, false);
        compare.fields(before.fields, after.fields, "/fields", 0);
        for (auto &row : compare.unknowns)
            compare.result.unavailable.push_back(std::move(row.second));
        compare.result.provenance = {{"beforeContextSha256", before.provenance.value("contextSha256", "")},
                                     {"afterContextSha256", after.provenance.value("contextSha256", "")},
                                     {"workNodes", compare.nodes},
                                     {"textBytes", compare.text}};
        options.control.check();
        return std::move(compare.result);
    } catch (const Json::exception &) {
        throw Error("default-source", "Invalid effective default comparison model");
    }
}
} // namespace cyuediff
