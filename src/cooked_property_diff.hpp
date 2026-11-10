#pragma once
#include <cyuediff/zen.hpp>
#include <set>
namespace cyuediff::detail {
using CookedWalk = void (*)(const Json &, const Json &, const std::string &, Json &,
                            const OperationControl &);
inline std::string cooked_token(const std::string &value) {
    std::string result;
    for (const auto c : value)
        result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return result;
}
// This traversal follows the reader's retained type inventory. Native program output
// is raw JSON, even when its member names happen to resemble property annotations.
struct CookedPropertyDiff {
    const Json &ac, &bc;
    Json &changes, &unavailable;
    const OperationControl &control;
    CookedWalk walk;
    ValueCoverage *coverage = nullptr;
    void check(unsigned depth) const {
        control.check();
        if (depth >= 256)
            throw Error("zen-limit", "Cooked correspondence nesting exceeds limit");
    }
    void unknown(const std::string &path, const char *reason) {
        control.check();
        unavailable.push_back({{"domain", "cooked-properties"}, {"path", path}, {"reason", reason}});
        if (coverage)
            coverage->comparison_unavailable.push_back(path);
    }
    void change(const std::string &path, const Json *before, const Json *after) {
        control.check();
        if (coverage)
            return; // Coverage-only traversal must not copy whole added native values.
        Json row = {{"op", before ? "remove" : "add"}, {"path", path}};
        if (before)
            row["before"] = *before;
        if (after)
            row["after"] = *after;
        changes.push_back(std::move(row));
    }
    const Json *schema(const Json &context, const std::string &name) const {
        const auto all = context.find("schemas");
        if (all == context.end() || !all->is_object())
            return nullptr;
        const auto it = all->find(name);
        return it == all->end() ? nullptr : &*it;
    }
    const Json *field(const Json &context, std::string name, const std::string &key,
                      std::uint64_t *slot = nullptr) const {
        std::uint64_t base = 0;
        for (unsigned d = 0;; ++d) {
            check(d);
            const auto *definition = schema(context, name);
            if (!definition || !definition->contains("fields"))
                return nullptr;
            const auto &fields = definition->at("fields");
            const auto it = fields.find(key);
            if (it != fields.end()) {
                if (slot)
                    *slot = base + it->at("index").get<std::uint64_t>();
                return &it->at("type");
            }
            base += definition->at("slots").get<std::uint64_t>();
            name = definition->value("super", "");
            if (name.empty())
                return nullptr;
        }
    }
    using Seen = std::set<std::pair<std::string, std::string>>;
    bool compatible_schema(const std::string &a, const std::string &b, Seen &seen, unsigned depth) const {
        check(depth);
        if (a != b)
            return false;
        const auto *as = schema(ac, a), *bs = schema(bc, b);
        if (!as || !bs)
            return false;
        if (!seen.emplace(a, b).second)
            return true;
        const auto &af = as->at("fields"), &bf = bs->at("fields");
        if (af.size() != bf.size() || as->at("slots") != bs->at("slots") ||
            as->value("super", "") != bs->value("super", ""))
            return false;
        for (auto it = af.begin(); it != af.end(); ++it) {
            control.check();
            const auto peer = bf.find(it.key());
            if (peer == bf.end() || it->at("index") != peer->at("index") ||
                !compatible(it->at("type"), peer->at("type"), seen, depth + 1))
                return false;
        }
        const auto super = as->value("super", "");
        return super.empty() || compatible_schema(super, super, seen, depth + 1);
    }
    bool compatible(const Json &a, const Json &b, Seen &seen, unsigned depth) const {
        check(depth);
        if (a.value("kind", 255) != b.value("kind", 255) || a.value("name", "") != b.value("name", ""))
            return false;
        const auto kind = a.value("kind", 255);
        if (kind == 9) {
            const auto route = a.value("route", "");
            if (route != b.value("route", "") || route.empty() || route == "unsupported")
                return false;
            if (route == "builtin")
                return a.at("layout") == b.at("layout");
            if ((route == "layout" || route == "hybrid") &&
                (a.value("definitionSha256", "").empty() ||
                 a.value("definitionSha256", "") != b.value("definitionSha256", "")))
                return false;
            return route == "layout" || compatible_schema(a.at("name"), b.at("name"), seen, depth + 1);
        }
        if (a.contains("referenceIdentity") &&
            (a.at("referenceIdentity") == "" || b.value("referenceIdentity", "").empty()))
            return false;
        if (a.contains("arguments") || b.contains("arguments")) {
            if (!a.contains("arguments") || !b.contains("arguments") ||
                a.at("arguments").size() != b.at("arguments").size())
                return false;
            for (std::size_t i = 0; i < a.at("arguments").size(); ++i)
                if (!compatible(a.at("arguments")[i], b.at("arguments")[i], seen, depth + 1))
                    return false;
            // Container-level format/encoding members must also agree.
            for (auto it = a.begin(); it != a.end(); ++it)
                if (it.key() != "arguments" && (!b.contains(it.key()) || b.at(it.key()) != it.value()))
                    return false;
            return a.size() == b.size();
        }
        return a == b;
    }
    bool compatible(const Json &a, const Json &b) const {
        Seen seen;
        return compatible(a, b, seen, 0);
    }
    void structure(const Json &a, const Json &b, const std::string &an, const std::string &bn,
                   const std::string &path, unsigned depth) {
        check(depth);
        if (an != bn || !schema(ac, an) || !schema(bc, bn)) {
            unknown(path, "cooked-schema-correspondence-unavailable");
            return;
        }
        auto visit = [&](const std::string &key, const Json *av, const Json *bv) {
            control.check();
            const auto child = path + "/" + cooked_token(key);
            std::uint64_t a_slot = 0, b_slot = 0;
            const auto *at = field(ac, an, key, &a_slot), *bt = field(bc, bn, key, &b_slot);
            if (!at || !bt) {
                unknown(child, "cooked-schema-field-unavailable");
                return;
            }
            if (a_slot != b_slot) {
                unknown(child, "cooked-schema-slot-incompatible");
                return;
            }
            if (!av || !bv) {
                if (!compatible(*at, *bt)) {
                    unknown(child, "cooked-reader-format-incompatible");
                    return;
                }
                change(child, av, bv);
                return;
            }
            walk(av->at("serializedZero"), bv->at("serializedZero"), child + "/serializedZero", changes,
                 control);
            value(av->at("value"), bv->at("value"), *at, *bt, child + "/value", depth + 1);
        };
        for (auto it = a.begin(); it != a.end(); ++it) {
            const auto peer = b.find(it.key());
            visit(it.key(), &it.value(), peer == b.end() ? nullptr : &*peer);
        }
        for (auto it = b.begin(); it != b.end(); ++it)
            if (!a.contains(it.key()))
                visit(it.key(), nullptr, &it.value());
    }
    void keyed(const Json &a, const Json &b, const Json &at, const Json &bt, bool pairs, const Json *avt,
               const Json *bvt, const std::string &path, unsigned depth) {
        check(depth);
        auto visit = [&](const std::string &key, const Json *av, const Json *bv) {
            const auto child = path + "/" + cooked_token(key);
            if (!av || !bv) {
                if (pairs && !compatible(*avt, *bvt)) {
                    unknown(child, "cooked-reader-format-incompatible");
                    return;
                }
                change(child, av, bv);
            } else if (pairs) {
                value(av->at("key"), bv->at("key"), at, bt, child + "/key", depth + 1);
                value(av->at("value"), bv->at("value"), *avt, *bvt, child + "/value", depth + 1);
            } else
                value(*av, *bv, at, bt, child, depth + 1);
        };
        for (auto it = a.begin(); it != a.end(); ++it) {
            control.check();
            const auto peer = b.find(it.key());
            visit(it.key(), &it.value(), peer == b.end() ? nullptr : &*peer);
        }
        for (auto it = b.begin(); it != b.end(); ++it)
            if (!a.contains(it.key()))
                visit(it.key(), nullptr, &it.value());
    }
    void value(const Json &a, const Json &b, const Json &at, const Json &bt, const std::string &path,
               unsigned depth) {
        check(depth);
        const auto kind = at.value("kind", 255);
        if (kind != bt.value("kind", 255) || at.value("name", "") != bt.value("name", "")) {
            unknown(path, "cooked-reader-type-incompatible");
            return;
        }
        if (kind == 9) {
            const auto route = at.value("route", "");
            if (route != bt.value("route", "") || route.empty() || route == "unsupported") {
                unknown(path, "cooked-reader-route-incompatible");
                return;
            }
            if (route == "reflected" || route == "hybrid") {
                structure(route == "hybrid" ? a.at("properties") : a,
                          route == "hybrid" ? b.at("properties") : b, at.at("name"), bt.at("name"),
                          route == "hybrid" ? path + "/properties" : path, depth + 1);
                if (route == "reflected")
                    return;
            }
            const bool native = route == "builtin";
            const bool same = native
                                  ? at.at("layout") == bt.at("layout")
                                  : !at.value("definitionSha256", "").empty() &&
                                        at.value("definitionSha256", "") == bt.value("definitionSha256", "");
            const auto target = route == "hybrid" ? path + "/native" : path;
            if (coverage && ((a.is_structured() && !a.empty()) || (b.is_structured() && !b.empty())))
                coverage->raw.push_back(target);
            if (!same) {
                unknown(target, "cooked-reader-definition-incompatible");
                return;
            }
            walk(route == "hybrid" ? a.at("native") : a, route == "hybrid" ? b.at("native") : b, target,
                 changes, control);
            return;
        }
        if (kind == 8) {
            const auto &ae = at.at("arguments")[0], &be = bt.at("arguments")[0];
            const auto shared = std::min(a.size(), b.size());
            for (std::size_t i = 0; i < shared; ++i)
                value(a[i], b[i], ae, be, path + "/" + std::to_string(i), depth + 1);
            if (a.size() != b.size()) {
                if (!compatible(ae, be))
                    unknown(path, "cooked-reader-format-incompatible");
                else {
                    for (auto i = shared; i < a.size(); ++i)
                        change(path + "/" + std::to_string(i), &a[i], nullptr);
                    for (auto i = shared; i < b.size(); ++i)
                        change(path + "/" + std::to_string(i), nullptr, &b[i]);
                }
            }
            return;
        }
        if (kind == 24 || kind == 25) {
            walk(a.at("replace"), b.at("replace"), path + "/replace", changes, control);
            const auto &ak = at.at("arguments")[0], &bk = bt.at("arguments")[0];
            if (!compatible(ak, bk)) {
                if (!a.at("entries").empty() || !b.at("entries").empty())
                    unknown(path + "/entries", "cooked-key-correspondence-unavailable");
                if (!a.at("removed").empty() || !b.at("removed").empty())
                    unknown(path + "/removed", "cooked-key-correspondence-unavailable");
                return;
            }
            keyed(a.at("removed"), b.at("removed"), ak, bk, false, nullptr, nullptr, path + "/removed",
                  depth + 1);
            const auto *av = kind == 24 ? &at.at("arguments")[1] : nullptr;
            const auto *bv = kind == 24 ? &bt.at("arguments")[1] : nullptr;
            keyed(a.at("entries"), b.at("entries"), ak, bk, kind == 24, av, bv, path + "/entries", depth + 1);
            return;
        }
        if (kind == 28) {
            if (at.value("optionalLayout", 0) != bt.value("optionalLayout", 0)) {
                unknown(path, "cooked-reader-format-incompatible");
                return;
            }
            walk(a.at("isSet"), b.at("isSet"), path + "/isSet", changes, control);
            const auto &av = at.at("arguments")[0], &bv = bt.at("arguments")[0];
            if (a.contains("value") && b.contains("value"))
                value(a.at("value"), b.at("value"), av, bv, path + "/value", depth + 1);
            else if (a.contains("value") || b.contains("value")) {
                if (!compatible(av, bv))
                    unknown(path + "/value", "cooked-reader-format-incompatible");
                else
                    change(path + "/value", a.contains("value") ? &a.at("value") : nullptr,
                           b.contains("value") ? &b.at("value") : nullptr);
            }
            return;
        }
        if (coverage && (a.is_structured() || b.is_structured()))
            coverage->raw.push_back(path);
        if (!compatible(at, bt) && !(kind == 4 && a.is_null() && b.is_null())) {
            unknown(path, "cooked-reader-format-incompatible");
            return;
        }
        walk(a, b, path, changes, control);
    }
};
} // namespace cyuediff::detail
