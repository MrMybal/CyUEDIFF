#include <cyuediff/visual_diff.hpp>
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
std::string text(const Json *v, std::size_t limit) {
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
        if (count < s.size())
            result += "...";
    } else
        result = v->dump(-1, ' ', false, Json::error_handler_t::replace);
    return result;
}
std::string type(const Json *v) {
    auto name = field(field(v, "type"), "name");
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
        result.rows.push_back({path, label_type, text(x, options.max_text_bytes),
                               text(y, options.max_text_bytes), classify_change(a, b), depth, bool(a),
                               bool(b)});
        if (unwrap) {
            Json am=Json::object(),bm=Json::object();
            bool type_changed=classify_change(field(a,"type"),field(b,"type"))!=ChangeKind::unchanged;
            auto metadata=[&](const Json *source,Json &target) {
                if (source) for (auto i=source->begin();i!=source->end();++i)
                    if (i.key()!="value" && (i.key()!="type" || type_changed)) target[i.key()]=i.value();
            };
            metadata(a,am); metadata(b,bm);
            if (!am.empty() || !bm.empty()) {
                if (depth>=options.max_depth) result.truncated=true;
                else visit(a?&am:nullptr,b?&bm:nullptr,path+"/@tag",depth+1);
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
} // namespace cyuediff
