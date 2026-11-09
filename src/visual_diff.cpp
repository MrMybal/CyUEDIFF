#include <cyuediff/visual_diff.hpp>
#include <cyuediff/property_page.hpp>
#include <algorithm>
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
    default:
        return "Unchanged";
    }
}
ChangeKind classify_change(const Json *before, const Json *after) {
    if (!before)
        return after ? ChangeKind::added : ChangeKind::unchanged;
    if (!after)
        return ChangeKind::removed;
    return *before == *after ? ChangeKind::unchanged : ChangeKind::modified;
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
    if (descriptor && descriptor->is_string())
        return descriptor->get<std::string>();
    auto name = field(descriptor, "name");
    return name && name->is_string() ? name->get<std::string>() : std::string{};
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
        !options.max_text_bytes || options.max_text_bytes > 65536)
        throw Error("property-view-limit", "Invalid property table limits");
    PropertyTable result;
    std::function<void(const Json *, const Json *, const std::string &, unsigned)> visit;
    visit = [&](const Json *a, const Json *b, const std::string &path, unsigned depth) {
        if (result.rows.size() >= options.max_rows) {
            result.truncated = true;
            return;
        }
        auto ta = type(a), tb = type(b);
        auto av = field(a, "value"), bv = field(b, "value");
        bool unwrap = (!a || (!ta.empty() && av)) && (!b || (!tb.empty() && bv));
        auto x = unwrap ? av : a, y = unwrap ? bv : b;
        std::string label_type;
        if (unwrap)
            label_type = ta == tb || tb.empty() ? ta : ta.empty() ? tb : ta + " -> " + tb;
        else
            label_type = y ? y->type_name() : x ? x->type_name() : "null";
        result.rows.push_back({path, label_type, text(x, options.max_text_bytes, options.escape_non_bmp),
                               text(y, options.max_text_bytes, options.escape_non_bmp), classify_change(a, b),
                               depth, bool(a), bool(b)});
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
                        if (i.key() != "value" && (i.key() != "type" || type_changed))
                            target[i.key()] = i.value();
            };
            metadata(a, am);
            metadata(b, bm);
            if (!am.empty() || !bm.empty()) {
                if (depth >= options.max_depth)
                    result.truncated = true;
                else
                    visit(a ? &am : nullptr, b ? &bm : nullptr, path + "/@tag", depth + 1);
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
                    visit(&*l, nullptr, path + "/" + escape(l.key()), depth + 1);
                    ++l;
                } else if (l == left.end() || r.key() < l.key()) {
                    visit(nullptr, &*r, path + "/" + escape(r.key()), depth + 1);
                    ++r;
                } else {
                    visit(&*l, &*r, path + "/" + escape(l.key()), depth + 1);
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
                      path + "/" + std::to_string(i), depth + 1);
            }
    };
    visit(before, after, "", 0);
    return result;
}

PropertyPage property_page(const Json *before, const Json *after, const std::string &scope,
                           const PropertyPageOptions &options) {
    if (!options.page_size || options.page_size > 512 || !options.max_compare_nodes ||
        options.max_compare_nodes > 64'000'000 || scope.size() > 4096 ||
        std::count(scope.begin(), scope.end(), '/') > 128)
        throw Error("property-page-limit", "Invalid property page limits or scope");
    options.control.check();
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
    PropertyPage result;
    result.scope = scope;
    result.available = a || b;
    if (!result.available)
        return result;
    std::size_t work = 0;
    std::function<bool(const Json &, const Json &, unsigned)> equal;
    equal = [&](const Json &x, const Json &y, unsigned depth) -> bool {
        if (++work > options.max_compare_nodes || depth > 128)
            throw Error("property-page-limit", "Property page comparison exceeds its budget");
        if ((work & 1023) == 0)
            options.control.checkpoint("property-page", work);
        if (&x == &y)
            return true;
        if (x.type() != y.type())
            return x.is_number() && y.is_number() && x == y;
        if (!x.is_structured())
            return x == y;
        if (x.size() != y.size())
            return false;
        auto i = x.begin(), j = y.begin();
        for (; i != x.end(); ++i, ++j) {
            if (x.is_object() && i.key() != j.key())
                return false;
            if (!equal(*i, *j, depth + 1))
                return false;
        }
        return true;
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
        auto tx = bounded_type(x), ty = bounded_type(y);
        auto vx = field(x, "value"), vy = field(y, "value");
        const bool unwrap = (!x || (!tx.empty() && vx)) && (!y || (!ty.empty() && vy));
        auto xv = unwrap ? vx : x, yv = unwrap ? vy : y;
        auto t = unwrap ? (tx == ty || ty.empty() ? tx
                           : tx.empty()           ? ty
                                                  : tx + " -> " + ty)
                        : std::string(yv   ? yv->type_name()
                                      : xv ? xv->type_name()
                                           : "null");
        auto change = !x                 ? (y ? ChangeKind::added : ChangeKind::unchanged)
                      : !y               ? ChangeKind::removed
                      : equal(*x, *y, 0) ? ChangeKind::unchanged
                                         : ChangeKind::modified;
        PropertyRow row{path,
                        t,
                        text(xv, 2048, options.escape_non_bmp),
                        text(yv, 2048, options.escape_non_bmp),
                        change,
                        depth,
                        bool(x),
                        bool(y)};
        row.label = label;
        result.table.rows.push_back(std::move(row));
        result.expandable.push_back((x && x->is_structured()) || (y && y->is_structured()));
    };
    append(a, b, scope, scope.empty() ? "/" : Json::json_pointer(scope).back(), 0);
    auto av = field(a, "value"), bv = field(b, "value");
    result.unwrapped =
        !options.serialization_fields && (!a || (!type(a).empty() && av)) && (!b || (!type(b).empty() && bv));
    const auto x = result.unwrapped ? av : a, y = result.unwrapped ? bv : b;
    const auto base = scope + (result.unwrapped ? "/value" : "");
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
