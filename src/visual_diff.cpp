#include <cyuediff/visual_diff.hpp>
#include <cyuediff/property_page.hpp>
#include <algorithm>
#include "json_equality.hpp"
#include <set>
namespace cyuediff {
const char *change_name(ChangeKind change) {
    switch (change) {
    case ChangeKind::added:
        return "Added";
    case ChangeKind::removed:
        return "Removed";
    case ChangeKind::modified:
        return "Modified";
    case ChangeKind::unknown:
        return "Unknown";
    default:
        return "Unchanged";
    }
}
ChangeKind classify_change(const Json *before, const Json *after) {
    if (!before)
        return after ? ChangeKind::added : ChangeKind::unchanged;
    if (!after)
        return ChangeKind::removed;
    return detail::json_equal(*before, *after) ? ChangeKind::unchanged : ChangeKind::modified;
}
void ChangeCounts::add(ChangeKind change) {
    switch (change) {
    case ChangeKind::added:
        ++added;
        break;
    case ChangeKind::removed:
        ++removed;
        break;
    case ChangeKind::modified:
        ++modified;
        break;
    case ChangeKind::unknown:
        ++unknown;
        break;
    default:
        ++unchanged;
        break;
    }
}
namespace {
const Json *field(const Json *object, const std::string &name) {
    if (!object || !object->is_object())
        return nullptr;
    auto it = object->find(name);
    return it == object->end() ? nullptr : &*it;
}
std::set<std::string> keys(const Json *a, const Json *b) {
    std::set<std::string> result;
    for (auto object : {a, b})
        if (object && object->is_object())
            for (auto it = object->begin(); it != object->end(); ++it)
                result.insert(it.key());
    return result;
}
std::string escape(const std::string &key) {
    std::string result;
    for (char c : key)
        result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return result;
}
std::string text(const Json *v, std::size_t limit, bool escape_non_bmp) {
    if (!v)
        return {};
    std::string result;
    if (v->is_object())
        result = "{" + std::to_string(v->size()) + "}";
    else if (v->is_array())
        result = "[" + std::to_string(v->size()) + "]";
    else if (v->is_string()) {
        auto &s = v->get_ref<const std::string &>();
        auto count = std::min(s.size(), limit);
        while (count < s.size() && count && (static_cast<unsigned char>(s[count]) & 0xc0) == 0x80)
            --count;
        result = Json(s.substr(0, count)).dump(-1, ' ', false, Json::error_handler_t::replace);
        if (escape_non_bmp) {
            std::string escaped;
            auto word = [&](unsigned value) {
                escaped += "\\u";
                static const char digits[] = "0123456789abcdef";
                for (int shift = 12; shift >= 0; shift -= 4)
                    escaped += digits[(value >> shift) & 15];
            };
            for (std::size_t i = 0; i < result.size();) {
                auto byte = [&](std::size_t at) { return static_cast<unsigned char>(result[at]); };
                if (byte(i) >= 0xf0 && byte(i) <= 0xf4 && result.size() - i >= 4) {
                    auto cp = ((byte(i) & 7u) << 18) | ((byte(i + 1) & 63u) << 12) |
                              ((byte(i + 2) & 63u) << 6) | (byte(i + 3) & 63u);
                    cp -= 0x10000;
                    word(0xd800 + (cp >> 10));
                    word(0xdc00 + (cp & 1023));
                    i += 4;
                } else
                    escaped += result[i++];
            }
            result = std::move(escaped);
        }
        if (count < s.size())
            result += "...";
    } else
        result = v->dump(-1, ' ', false, Json::error_handler_t::replace);
    return result;
}
std::string type(const Json *v) {
    auto descriptor = field(v, "type");
    const auto name = descriptor && descriptor->is_string() ? descriptor : field(descriptor, "name");
    if (!name || !name->is_string())
        return {};
    const auto &text = name->get_ref<const std::string &>();
    return text.size() > 8 && text.compare(text.size() - 8, 8, "Property") == 0 ? text : std::string{};
}
bool opaque_value(const Json *v) {
    return v && !type(v).empty() && !v->contains("value") && v->contains("opaque");
}
bool skipped_value(const Json *v) {
    if (!v || !v->is_object() || type(v).empty())
        return false;
    auto i = v->find("serializationFlags");
    return i != v->end() && i->is_number_integer() &&
           (!i->is_number_unsigned() || i->get<std::uint64_t>() <= 63) && i->get<std::int64_t>() >= 0 &&
           (i->get<unsigned>() & 0x20);
}
bool same_opaque(const Json *a, const Json *b) {
    if (!opaque_value(a) || !opaque_value(b))
        return false;
    const auto &x = a->at("opaque"), &y = b->at("opaque");
    if (!x.is_object() || !y.is_object() || !x.contains("bytes") || !y.contains("bytes") ||
        !x.contains("sha256") || !y.contains("sha256") || !x["sha256"].is_string() ||
        !y["sha256"].is_string())
        return false;
    if (!x["bytes"].is_number_integer() || !y["bytes"].is_number_integer() ||
        (!x["bytes"].is_number_unsigned() && x["bytes"].get<std::int64_t>() < 0) ||
        (!y["bytes"].is_number_unsigned() && y["bytes"].get<std::int64_t>() < 0))
        return false;
    for (const auto *key : {"type", "guid", "serializationFlags", "extensions", "wireTypes"}) {
        const auto u = field(a, key), v = field(b, key);
        if (bool(u) != bool(v) || (u && *u != *v))
            return false;
    }
    const auto u = field(a, "resolvedType"), v = field(b, "resolvedType");
    if ((u ? *u : a->at("type")) != (v ? *v : b->at("type")))
        return false;
    const auto &hash = x["sha256"].get_ref<const std::string &>();
    return hash.size() == 64 && hash.find_first_not_of("0123456789abcdef") == std::string::npos &&
           x["bytes"] == y["bytes"] && x["sha256"] == y["sha256"];
}
class CoverageIndex {
    std::set<std::string> before_, after_, unknown_, ignored_, raw_;
    const Json *root_before_, *root_after_;
    bool cooked_;
    static bool cooked_tag(const Json *value) {
        return value && value->is_object() && !type(value).empty() && value->contains("value") &&
               value->contains("schemaIndex") && value->at("schemaIndex").is_number_unsigned() &&
               value->contains("serializedZero") && value->at("serializedZero").is_boolean();
    }
    bool cooked_annotation(std::string path) const {
        if (!cooked_ || raw(path))
            return false;
        for (;;) {
            const auto slash = path.find_last_of('/');
            if (slash == std::string::npos)
                return false;
            const auto key = path.substr(slash + 1);
            path.resize(slash);
            if (key == "value" || key == "serializedZero")
                continue;
            const Json::json_pointer pointer(path);
            for (const auto *root : {root_before_, root_after_}) {
                if (!root)
                    continue;
                try {
                    if (cooked_tag(&root->at(pointer)))
                        return true;
                } catch (const Json::exception &) {
                }
            }
        }
    }

  public:
    explicit CoverageIndex(const ValueCoverage &v, const Json *before, const Json *after)
        : root_before_(before), root_after_(after), cooked_(v.cooked_properties) {
        std::size_t count = 0, bytes = 0;
        auto load = [&](const auto &paths, auto &set) {
            for (const auto &path : paths) {
                if (++count > 400'000 || path.size() > 65536 || path.size() > 64 * 1024 * 1024 - bytes)
                    throw Error("property-coverage-limit", "Property coverage exceeds its budget");
                bytes += path.size();
                try {
                    Json::json_pointer validated(path);
                    (void)validated;
                } catch (const Json::exception &) {
                    throw Error("property-coverage-scope", "Invalid property coverage pointer");
                }
                set.insert(path);
            }
        };
        load(v.before_unavailable, before_);
        load(v.after_unavailable, after_);
        load(v.comparison_unavailable, unknown_);
        load(v.ignored, ignored_);
        load(v.raw, raw_);
    }
    static bool covers(const std::set<std::string> &set, std::string path) {
        for (;;) {
            if (set.count(path))
                return true;
            const auto slash = path.find_last_of('/');
            if (slash == std::string::npos)
                break;
            path.resize(slash);
        }
        return false;
    }
    static bool intersects(const std::set<std::string> &set, const std::string &path) {
        if (covers(set, path))
            return true;
        const auto prefix = path + "/";
        auto i = set.lower_bound(prefix);
        return i != set.end() && i->compare(0, prefix.size(), prefix) == 0;
    }
    bool unavailable(const std::string &p, bool after) const { return covers(after ? after_ : before_, p); }
    bool comparison(const std::string &p) const { return covers(unknown_, p); }
    bool uncertain(const std::string &p) const {
        return intersects(before_, p) || intersects(after_, p) || intersects(unknown_, p);
    }
    bool ignored(const std::string &p) const { return covers(ignored_, p) || cooked_annotation(p); }
    bool context_intersects(const std::string &p) const { return cooked_ || intersects(ignored_, p); }
    bool raw(const std::string &p) const { return covers(raw_, p); }
    bool uncertain_at(const std::string &p) const {
        return covers(before_, p) || covers(after_, p) || covers(unknown_, p);
    }
    bool empty_ignored() const { return ignored_.empty(); }
    bool equal(const Json *a, const Json *b, const std::string &path, std::size_t &work, std::size_t max,
               const OperationControl &control, unsigned depth = 0, bool known_only = false) const {
        if (ignored(path) || (known_only && uncertain_at(path)))
            return true;
        if (++work > max || depth > 256)
            throw Error("property-view-limit", "Property comparison exceeds its budget");
        if (!(work & 1023))
            control.checkpoint("property-values", work);
        if (!a || !b)
            return a == b;
        if (a == b)
            return true;
        if (a->type() != b->type())
            return a->is_number() && b->is_number() && detail::numeric_equal(*a, *b);
        if (a->is_object()) {
            auto i = a->begin(), j = b->begin();
            while (i != a->end() || j != b->end()) {
                if (j == b->end() || (i != a->end() && i.key() < j.key())) {
                    if (!equal(&*i, nullptr, path + "/" + escape(i.key()), work, max, control, depth + 1,
                               known_only))
                        return false;
                    ++i;
                } else if (i == a->end() || j.key() < i.key()) {
                    if (!equal(nullptr, &*j, path + "/" + escape(j.key()), work, max, control, depth + 1,
                               known_only))
                        return false;
                    ++j;
                } else {
                    if (!equal(&*i, &*j, path + "/" + escape(i.key()), work, max, control, depth + 1,
                               known_only))
                        return false;
                    ++i;
                    ++j;
                }
            }
            return true;
        }
        if (a->is_array()) {
            if (a->size() != b->size())
                return false;
            for (std::size_t i = 0; i < a->size(); ++i)
                if (!equal(&(*a)[i], &(*b)[i], path + "/" + std::to_string(i), work, max, control, depth + 1,
                           known_only))
                    return false;
            return true;
        }
        return *a == *b;
    }
    bool available(const Json *v, const std::string &p, bool after, bool raw = false) const {
        return !unavailable(p, after) && (raw || (!opaque_value(v) && !skipped_value(v))) &&
               (v || !comparison(p));
    }
};
struct ValueContext {
    bool raw = false, unavailable = false;
};
ValueContext value_context(const Json *root, const std::string &path, const CoverageIndex &coverage) {
    if (coverage.raw(path))
        return {true, false};
    if (!root)
        return {};
    const Json *current = root;
    bool hybrid_data = false;
    std::size_t first = path.empty() ? 0 : 1;
    while (first && first <= path.size() && current) {
        const auto last = path.find('/', first);
        auto key = path.substr(first, last == std::string::npos ? std::string::npos : last - first);
        std::string token;
        for (std::size_t i = 0; i < key.size(); ++i) {
            if (key[i] == '~' && i + 1 < key.size() && (key[i + 1] == '0' || key[i + 1] == '1'))
                token += key[++i] == '0' ? '~' : '/';
            else
                token += key[i];
        }
        if (hybrid_data) {
            if (token != "properties")
                return {true, false};
            hybrid_data = false;
        } else if (current->is_object() && current->contains("decoder") &&
                   (*current)["decoder"].is_string() && current->contains("data") && token == "data") {
            if (current->value("prefix", Json{}) != "tagged-properties")
                return {true, false};
            hybrid_data = true;
        } else if (!type(current).empty()) {
            if (token != "value")
                return {true, false};
            if (opaque_value(current) || skipped_value(current))
                return {false, true};
        }
        if (current->is_object()) {
            auto i = current->find(token);
            current = i == current->end() ? nullptr : &*i;
        } else if (current->is_array()) {
            std::size_t index = 0;
            bool valid = !token.empty();
            for (char c : token) {
                if (c < '0' || c > '9' || index > current->size() / 10) {
                    valid = false;
                    break;
                }
                index = index * 10 + static_cast<unsigned>(c - '0');
            }
            current = valid && index < current->size() ? &(*current)[index] : nullptr;
        } else
            current = nullptr;
        if (last == std::string::npos)
            break;
        first = last + 1;
    }
    return {};
}
} // namespace
GraphChanges graph_changes(const Json *before, const Json *after) {
    GraphChanges result;
    auto a = field(before, "nodes"), b = field(after, "nodes");
    for (const auto &key : keys(a, b))
        result.nodes[key] = classify_change(field(a, key), field(b, key));
    a = field(before, "links");
    b = field(after, "links");
    for (const auto &key : keys(a, b)) {
        auto x = field(a, key), y = field(b, key);
        auto change = classify_change(x, y);
        result.links[key] = change;
        result.link_counts.add(change);
        if (change == ChangeKind::unchanged)
            continue;
        for (auto link : {x, y})
            for (auto endpoint : {"from", "to"}) {
                auto node = field(field(link, endpoint), "node");
                if (!node || !node->is_string())
                    continue;
                auto it = result.nodes.find(node->get<std::string>());
                if (it != result.nodes.end() && it->second == ChangeKind::unchanged)
                    it->second = ChangeKind::modified;
            }
    }
    for (const auto &item : result.nodes)
        result.node_counts.add(item.second);
    return result;
}
PropertyTable property_table(const Json *before, const Json *after, const PropertyTableOptions &options) {
    if (!options.max_rows || options.max_rows > 1000000 || options.max_depth > 128 ||
        !options.max_text_bytes || options.max_text_bytes > 65536 || !options.max_compare_nodes ||
        options.max_compare_nodes > 64'000'000)
        throw Error("property-view-limit", "Invalid property table limits");
    options.control.check();
    CoverageIndex coverage(options.coverage, before, after);
    std::size_t work = 0;
    PropertyTable result;
    std::function<void(const Json *, const Json *, const std::string &, const std::string &, unsigned, bool)>
        visit;
    visit = [&](const Json *a, const Json *b, const std::string &path, const std::string &source,
                unsigned depth, bool metadata) {
        if (result.rows.size() >= options.max_rows) {
            result.truncated = true;
            return;
        }
        const auto ac = value_context(before, source, coverage), bc = value_context(after, source, coverage);
        const bool ra = metadata || ac.raw, rb = metadata || bc.raw;
        auto ta = ra ? std::string{} : type(a), tb = rb ? std::string{} : type(b);
        auto av = field(a, "value"), bv = field(b, "value");
        bool unwrap = (!a || (!ta.empty() && (av || opaque_value(a) || skipped_value(a)))) &&
                      (!b || (!tb.empty() && (bv || opaque_value(b) || skipped_value(b))));
        const auto value_path = source + (unwrap ? "/value" : "");
        const bool ka = !ac.unavailable && coverage.available(a, value_path, false, ra),
                   kb = !bc.unavailable && coverage.available(b, value_path, true, rb);
        auto x = unwrap ? av : a, y = unwrap ? bv : b;
        std::string label_type;
        if (unwrap)
            label_type = ta == tb || tb.empty() ? ta : ta.empty() ? tb : ta + " -> " + tb;
        else
            label_type = y ? y->type_name() : x ? x->type_name() : "null";
        const bool uncertain = metadata ? coverage.uncertain_at(source) : coverage.uncertain(source);
        auto change =
            !ka || !kb ? ChangeKind::unknown
            : coverage.equal(a, b, source, work, options.max_compare_nodes, options.control, 0, true)
                ? (uncertain ? ChangeKind::unknown : ChangeKind::unchanged)
            : !a ? ChangeKind::added
            : !b ? ChangeKind::removed
                 : ChangeKind::modified;
        if (!ra && !rb && same_opaque(a, b) && !coverage.uncertain(source))
            change = ChangeKind::unchanged;
        result.rows.push_back({path, label_type,
                               ka ? text(x, options.max_text_bytes, options.escape_non_bmp) : "Unavailable",
                               kb ? text(y, options.max_text_bytes, options.escape_non_bmp) : "Unavailable",
                               change, depth, bool(a), bool(b)});
        result.rows.back().before_available = ka;
        result.rows.back().after_available = kb;
        result.rows.back().source_path = source;
        result.rows.back().context_only =
            change != ChangeKind::unknown &&
            (coverage.ignored(source) ||
             (change == ChangeKind::unchanged && a && b && *a != *b && coverage.context_intersects(source)));
        if (!ka)
            x = nullptr;
        if (!kb)
            y = nullptr;
        auto &display = result.rows.back().label;
        auto slash = path.find_last_of('/');
        auto token =
            path.empty() ? std::string("/") : path.substr(slash == std::string::npos ? 0 : slash + 1);
        for (std::size_t j = 0; j < token.size(); ++j) {
            if (token[j] == '~' && j + 1 < token.size() && (token[j + 1] == '0' || token[j + 1] == '1'))
                display += token[++j] == '0' ? '~' : '/';
            else
                display += token[j];
        }
        if (unwrap) {
            Json am = Json::object(), bm = Json::object();
            bool type_changed = classify_change(field(a, "type"), field(b, "type")) != ChangeKind::unchanged;
            auto metadata = [&](const Json *source, Json &target) {
                if (source)
                    for (auto i = source->begin(); i != source->end(); ++i)
                        if (i.key() != "value" && i.key() != "resolvedType" && i.key() != "readFormats" &&
                            (i.key() != "type" || type_changed))
                            target[i.key()] = i.value();
            };
            metadata(a, am);
            metadata(b, bm);
            if (!am.empty() || !bm.empty()) {
                if (depth >= options.max_depth)
                    result.truncated = true;
                else
                    visit(a ? &am : nullptr, b ? &bm : nullptr, path + "/@tag", source, depth + 1, true);
            }
        }
        bool object = (!x || x->is_object()) && (!y || y->is_object());
        bool array = (!x || x->is_array()) && (!y || y->is_array());
        if (!object && !array)
            return;
        auto count = std::max(x ? x->size() : 0, y ? y->size() : 0);
        if (!count)
            return;
        if (depth >= options.max_depth) {
            result.truncated = true;
            return;
        }
        if (object) {
            const Json empty = Json::object();
            const auto &left = x ? *x : empty, &right = y ? *y : empty;
            auto l = left.begin(), r = right.begin();
            while (l != left.end() || r != right.end()) {
                if (result.rows.size() >= options.max_rows) {
                    result.truncated = true;
                    break;
                }
                if (r == right.end() || (l != left.end() && l.key() < r.key())) {
                    visit(&*l, nullptr, path + "/" + escape(l.key()), value_path + "/" + escape(l.key()),
                          depth + 1, false);
                    ++l;
                } else if (l == left.end() || r.key() < l.key()) {
                    visit(nullptr, &*r, path + "/" + escape(r.key()), value_path + "/" + escape(r.key()),
                          depth + 1, false);
                    ++r;
                } else {
                    visit(&*l, &*r, path + "/" + escape(l.key()), value_path + "/" + escape(l.key()),
                          depth + 1, false);
                    ++l;
                    ++r;
                }
            }
        } else
            for (std::size_t i = 0; i < count; ++i) {
                if (result.rows.size() >= options.max_rows) {
                    result.truncated = true;
                    break;
                }
                visit(x && i < x->size() ? &x->at(i) : nullptr, y && i < y->size() ? &y->at(i) : nullptr,
                      path + "/" + std::to_string(i), value_path + "/" + std::to_string(i), depth + 1, false);
            }
    };
    visit(before, after, "", "", 0, false);
    return result;
}

PropertyPage property_page(const Json *before, const Json *after, const std::string &scope,
                           const PropertyPageOptions &options) {
    if (!options.page_size || options.page_size > 512 || !options.max_compare_nodes ||
        options.max_compare_nodes > 64'000'000 || scope.size() > 4096 ||
        std::count(scope.begin(), scope.end(), '/') > 128)
        throw Error("property-page-limit", "Invalid property page limits or scope");
    options.control.check();
    CoverageIndex coverage(options.coverage, before, after);
    std::size_t work = 0;
    Json::json_pointer pointer;
    try {
        pointer = Json::json_pointer(scope);
    } catch (const Json::exception &) {
        throw Error("property-page-scope", "Invalid property JSON pointer");
    }
    auto resolve = [&](const Json *root) -> const Json * {
        if (!root)
            return nullptr;
        try {
            return &root->at(pointer);
        } catch (const Json::exception &) {
            return nullptr;
        }
    };
    const auto a = resolve(before), b = resolve(after);
    const auto ac = value_context(before, scope, coverage), bc = value_context(after, scope, coverage);
    PropertyPage result;
    result.scope = scope;
    result.available = a || b || ac.unavailable || bc.unavailable || coverage.uncertain(scope);
    if (!result.available)
        return result;

    const auto page_equal = [&](const Json *a, const Json *b, const std::string &path) {
        try {
            return coverage.equal(a, b, path, work, options.max_compare_nodes, options.control, 0, true);
        } catch (const Error &e) {
            if (e.code == "property-view-limit")
                throw Error("property-page-limit", e.what());
            throw;
        }
    };
    auto append = [&](const Json *x, const Json *y, const std::string &path, const std::string &label,
                      unsigned depth) {
        options.control.check();
        if (path.size() > 4096)
            throw Error("property-page-limit", "Child path exceeds its scope budget");
        auto bounded_type = [&](const Json *value) {
            auto name = type(value);
            if (name.size() > 2048) {
                std::size_t end = 2048;
                while (end && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80)
                    --end;
                name.resize(end);
                name += "...";
            }
            return name;
        };
        const auto xc = value_context(before, path, coverage), yc = value_context(after, path, coverage);
        const bool rx = xc.raw, ry = yc.raw;
        auto tx = rx ? std::string{} : bounded_type(x), ty = ry ? std::string{} : bounded_type(y);
        auto vx = field(x, "value"), vy = field(y, "value");
        const bool unwrap = (!x || (!tx.empty() && (vx || opaque_value(x) || skipped_value(x)))) &&
                            (!y || (!ty.empty() && (vy || opaque_value(y) || skipped_value(y))));
        const auto value_path = path + (unwrap ? "/value" : "");
        const bool kx = !xc.unavailable && coverage.available(x, value_path, false, rx),
                   ky = !yc.unavailable && coverage.available(y, value_path, true, ry);
        auto xv = unwrap ? vx : x, yv = unwrap ? vy : y;
        auto t = unwrap ? (tx == ty || ty.empty() ? tx
                           : tx.empty()           ? ty
                                                  : tx + " -> " + ty)
                        : std::string(yv   ? yv->type_name()
                                      : xv ? xv->type_name()
                                           : "null");
        const bool uncertain = coverage.uncertain(path);
        auto change = !kx || !ky               ? ChangeKind::unknown
                      : page_equal(x, y, path) ? (uncertain ? ChangeKind::unknown : ChangeKind::unchanged)
                      : !x                     ? ChangeKind::added
                      : !y                     ? ChangeKind::removed
                                               : ChangeKind::modified;
        if (!rx && !ry && same_opaque(x, y) && !coverage.uncertain(path))
            change = ChangeKind::unchanged;
        PropertyRow row{path,
                        t,
                        kx ? text(xv, 2048, options.escape_non_bmp) : "Unavailable",
                        ky ? text(yv, 2048, options.escape_non_bmp) : "Unavailable",
                        change,
                        depth,
                        bool(x),
                        bool(y)};
        row.before_available = kx;
        row.after_available = ky;
        row.source_path = path;
        row.context_only = change != ChangeKind::unknown &&
                           (coverage.ignored(path) || (change == ChangeKind::unchanged && x && y &&
                                                       *x != *y && coverage.context_intersects(path)));
        row.label = label;
        result.table.rows.push_back(std::move(row));
        result.expandable.push_back((kx && x && x->is_structured()) || (ky && y && y->is_structured()));
    };
    append(a, b, scope, scope.empty() ? "/" : Json::json_pointer(scope).back(), 0);
    const bool ra = ac.raw, rb = bc.raw;
    auto av = field(a, "value"), bv = field(b, "value");
    result.unwrapped = !options.serialization_fields &&
                       (!a || (!ra && !type(a).empty() && (av || opaque_value(a) || skipped_value(a)))) &&
                       (!b || (!rb && !type(b).empty() && (bv || opaque_value(b) || skipped_value(b))));
    const auto base = scope + (result.unwrapped ? "/value" : "");
    // Serialization fields remain readable even when this tag's value is opaque.
    // Explicit side masks and an unread ancestor value still take precedence.
    const auto x = !ac.unavailable && coverage.available(a, base, false, ra || options.serialization_fields)
                       ? (result.unwrapped ? av : a)
                       : nullptr,
               y = !bc.unavailable && coverage.available(b, base, true, rb || options.serialization_fields)
                       ? (result.unwrapped ? bv : b)
                       : nullptr;
    const bool object = (!x || x->is_object()) && (!y || y->is_object());
    result.array = (!x || x->is_array()) && (!y || y->is_array());
    if (!object && !result.array)
        return result;
    if (result.array)
        result.total_children = std::max(x ? x->size() : 0, y ? y->size() : 0);
    else {
        const Json empty = Json::object();
        const auto &left = x ? *x : empty, &right = y ? *y : empty;
        auto l = left.begin(), rr = right.begin();
        while (l != left.end() || rr != right.end()) {
            if ((result.total_children & 1023) == 0)
                options.control.check();
            if (++result.total_children > 2'000'000)
                throw Error("property-page-limit", "Property page child count exceeds its budget");
            if (rr == right.end() || (l != left.end() && l.key() < rr.key()))
                ++l;
            else if (l == left.end() || rr.key() < l.key())
                ++rr;
            else {
                ++l;
                ++rr;
            }
        }
    }
    if (result.total_children > 2'000'000)
        throw Error("property-page-limit", "Property page child count exceeds its budget");
    result.first =
        result.total_children
            ? std::min(options.first, ((result.total_children - 1) / options.page_size) * options.page_size)
            : 0;
    const auto last = std::min(result.total_children, result.first + options.page_size);
    if (result.array) {
        for (auto i = result.first; i < last; ++i)
            append(x && i < x->size() ? &x->at(i) : nullptr, y && i < y->size() ? &y->at(i) : nullptr,
                   base + "/" + std::to_string(i), std::to_string(i), 1);
    } else {
        const Json empty = Json::object();
        const auto &left = x ? *x : empty, &right = y ? *y : empty;
        auto l = left.begin(), rr = right.begin();
        std::size_t index = 0;
        while (index < last) {
            if ((index & 1023) == 0)
                options.control.check();
            const Json *lv = nullptr, *rv = nullptr;
            std::string key;
            if (rr == right.end() || (l != left.end() && l.key() < rr.key())) {
                key = l.key();
                lv = &*l++;
            } else if (l == left.end() || rr.key() < l.key()) {
                key = rr.key();
                rv = &*rr++;
            } else {
                key = l.key();
                lv = &*l++;
                rv = &*rr++;
            }
            if (index++ >= result.first)
                append(lv, rv, base + "/" + escape(key), key, 1);
        }
    }
    options.control.checkpoint("property-page", work, work);
    return result;
}
} // namespace cyuediff
