#pragma once
#include <cyuediff/core.hpp>
#include <cmath>
#include <set>
namespace cyuediff::detail {
inline bool source_key_tag(const Json &value) {
    if (!value.is_object() || (!value.contains("value") && !value.contains("opaque")))
        return false;
    auto i = value.find("type");
    if (i == value.end())
        return false;
    if (i->is_object()) {
        i = i->find("name");
        if (i == value["type"].end())
            return false;
    }
    if (!i->is_string())
        return false;
    const auto &name = i->get_ref<const std::string &>();
    return name.size() > 8 && name.compare(name.size() - 8, 8, "Property") == 0;
}
// Canonical stored source key JSON, excluding only reader context in known tag wrappers.
// Native callback data is raw; hybrid reflected prefixes retain their tag semantics.
template <class Charge>
std::string source_key_identity(const Json &key, const OperationControl &control, Charge charge,
                                bool normalize = true) {
    enum class Mode { source, raw, hybrid };
    std::string output;
    auto append = [&](const std::string &s) {
        charge(s.size());
        output += s;
    };
    auto string = [&](const std::string &s) {
        std::uint64_t size = 2;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (!(i & 4095))
                control.check();
            const auto c = static_cast<unsigned char>(s[i]);
            size += c == '"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t'
                        ? 2
                        : (c < 32 ? 6 : 1);
        }
        charge(size);
        output += Json(s).dump();
    };
    std::size_t nodes = 0;
    std::function<void(const Json &, Mode, unsigned)> write = [&](const Json &v, Mode mode, unsigned depth) {
        if (depth > 512)
            throw Error("limit", "Source key identity nesting exceeds its limit");
        if (!(nodes++ & 1023))
            control.check();
        if (v.is_string())
            string(v.get_ref<const std::string &>());
        else if (v.is_array()) {
            append("[");
            bool first = true;
            for (const auto &child : v) {
                if (!first)
                    append(",");
                first = false;
                write(child, mode == Mode::hybrid ? Mode::raw : mode, depth + 1);
            }
            append("]");
        } else if (v.is_object()) {
            const bool wrapper = mode == Mode::source && v.contains("decoder") && v["decoder"].is_string() &&
                                 v.contains("data");
            const bool tag = mode == Mode::source && !wrapper && source_key_tag(v);
            append("{");
            bool first = true;
            for (auto i = v.begin(); i != v.end(); ++i) {
                if (tag && (i.key() == "resolvedType" || i.key() == "readFormats"))
                    continue;
                if (!first)
                    append(",");
                first = false;
                string(i.key());
                append(":");
                auto child = mode;
                if (wrapper)
                    child =
                        i.key() == "data"
                            ? (v.value("prefix", Json{}) == "tagged-properties" ? Mode::hybrid : Mode::raw)
                            : Mode::raw;
                else if (mode == Mode::hybrid)
                    child = i.key() == "properties" ? Mode::source : Mode::raw;
                else if (tag && i.key() != "value")
                    child = Mode::raw;
                write(i.value(), child, depth + 1);
            }
            append("}");
        } else {
            if (v.is_binary() || v.is_discarded() || (v.is_number_float() && !std::isfinite(v.get<double>())))
                throw Error("key-identity", "Unsupported source key value");
            append(v.dump());
        }
    };
    write(key, normalize ? Mode::source : Mode::raw, 0);
    return output;
}
// Legacy keys are canonical JSON; typed keys use a disjoint, explicitly versioned domain.
inline std::string key_encoding(const std::string &id) {
    if (id.compare(0, 8, "ue-name/") != 0)
        return "json";
    const auto end = id.find(':', 8);
    return end != std::string::npos && end <= 64 ? id.substr(0, end) : "ue-name/unknown";
}
template <class Visit> void collection_key_encodings(const Json &value, Visit visit) {
    for (const auto *part : {"removed", "entries"}) {
        const auto found = value.find(part);
        if (found == value.end() || !found->is_object())
            continue;
        for (auto it = found->begin(); it != found->end(); ++it)
            visit(key_encoding(it.key()));
    }
}
inline bool compatible_key_encodings(const Json &a, const Json &b, const OperationControl &control) {
    std::set<std::string> af, bf;
    collection_key_encodings(a, [&](const std::string &format) {
        control.check();
        af.insert(format);
    });
    collection_key_encodings(b, [&](const std::string &format) {
        control.check();
        bf.insert(format);
    });
    // An empty peer has no IDs to reinterpret. Mixed domains stay conservative.
    return af.empty() || bf.empty() || af == bf;
}
// Distinguish real serialized names from raw JSON objects that merely resemble them.
// The display tree remains readable; names are indexed by their actual value-relative paths.
template <class Canonical, class Charge>
std::string named_key_identity(const Json &key, const Json &names, Canonical canonical, Charge charge) {
    if (names.empty())
        return canonical(key, false);
    auto append = [&](std::string &out, const std::string &text) {
        charge(text.size());
        out += text;
    };
    std::string result;
    append(result, "ue-name/1:[");
    result += canonical(key, false);
    append(result, ",");
    result += canonical(names, true);
    append(result, "]");
    return result;
}
} // namespace cyuediff::detail
