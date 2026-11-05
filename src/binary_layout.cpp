#include <cyuediff/binary_layout.hpp>
#include "string_value.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
namespace cyuediff {
namespace {
void require(bool condition, const char *message) {
    if (!condition) throw Error("layout-format", message);
}
void fields(const Json &value, std::initializer_list<const char *> allowed) {
    require(value.is_object(), "Expected a binary layout object");
    for (auto it = value.begin(); it != value.end(); ++it)
        require(std::any_of(allowed.begin(), allowed.end(), [&](const char *key) { return it.key() == key; }),
                "Unknown binary layout field");
}
std::string word(const Json &value) {
    require(value.is_string(), "Expected a binary layout string");
    auto result = value.get<std::string>();
    require(!result.empty() && result.find('\0') == std::string::npos, "Invalid binary layout identity");
    return result;
}
struct DefinitionValidator {
    const BinaryLayoutOptions &options;
    std::uint32_t nodes = 0;
    std::uint64_t text_bytes = 0;
    void json(const Json &value, unsigned depth = 0) {
        options.control.check();
        if (depth >= options.max_depth || nodes >= options.max_nodes)
            throw Error("layout-limit", "Layout definition exceeds node/depth limit");
        ++nodes;
        auto text = [&](const std::string &s) {
            if (s.size() > options.max_string_bytes || s.size() > options.max_text_bytes - text_bytes)
                throw Error("layout-limit", "Layout definition exceeds text limit");
            detail::validate_byte_string(s, false, options.control, "layout-format", 0);
            text_bytes += s.size();
        };
        if (value.is_string()) text(value.get_ref<const std::string &>());
        else if (value.is_structured())
            for (auto it = value.begin(); it != value.end(); ++it) {
                if (value.is_object()) text(it.key());
                json(it.value(), depth + 1);
            }
        else require(!value.is_binary() && !value.is_discarded() &&
                         (!value.is_number_float() || std::isfinite(value.get<double>())),
                     "Layout definition must be ordinary finite JSON");
    }
};
void node(const Json &value, std::set<std::string> &references) {
    require(value.is_object(), "Expected a layout type object");
    const auto type = word(value.at("type"));
    if (type == "record") {
        fields(value, {"type", "fields"});
        const auto &members = value.at("fields");
        require(members.is_array(), "Expected ordered record fields");
        std::set<std::string> names;
        for (const auto &member : members) {
            fields(member, {"name", "value"});
            require(names.insert(word(member.at("name"))).second, "Duplicate layout record field");
            node(member.at("value"), references);
        }
    } else if (type == "array" || type == "bytes") {
        if (type == "array") {
            fields(value, {"type", "count", "element"});
            node(value.at("element"), references);
        } else fields(value, {"type", "count"});
        const auto &count = value.at("count");
        require((count.is_number_unsigned() && count.get<std::uint64_t>() <= UINT32_MAX) ||
                    (count.is_number_integer() && !count.is_number_unsigned() &&
                     count.get<std::int64_t>() >= 0 && count.get<std::int64_t>() <= UINT32_MAX) || count == "i32",
                "Layout count must be a uint32 constant or i32 prefix");
    } else if (type == "optional") {
        fields(value, {"type", "presence", "value"});
        require(value.at("presence") == "bool8" || value.at("presence") == "bool32",
                "Unsupported optional presence layout");
        node(value.at("value"), references);
    } else if (type == "ref") {
        fields(value, {"type", "layout"});
        references.insert(word(value.at("layout")));
    } else {
        fields(value, {"type"});
        static const std::set<std::string> scalars = {"u8", "i8", "u16", "i16", "u32", "i32",
            "u64", "i64", "f32", "f64", "bool8", "bool32", "string", "name", "guid", "object"};
        require(scalars.count(type) != 0, "Unsupported binary layout type");
    }
}
VersionRange version_range(const Json &value) {
    require(value.is_array() && value.size() == 2, "Expected an inclusive version interval");
    for (const auto &v : value)
        require(v.is_number_integer() &&
                    (v.is_number_unsigned() ? v.get<std::uint64_t>() <= INT32_MAX
                                           : v.get<std::int64_t>() >= INT32_MIN && v.get<std::int64_t>() <= INT32_MAX),
                "Version interval requires int32 endpoints");
    VersionRange result{value[0].get<int>(), value[1].get<int>()};
    require(result.min <= result.max, "Inverted version interval");
    return result;
}
AssetDecoder binding(const Json &value) {
    fields(value, {"id", "kind", "target", "profile", "layout"});
    AssetDecoder result;
    result.id = word(value.at("id")); result.target = word(value.at("target"));
    const auto kind = word(value.at("kind"));
    require(kind == "struct" || kind == "native-export", "Unsupported layout decoder kind");
    result.kind = kind == "struct" ? DecoderKind::struct_value : DecoderKind::native_export;
    const auto &profile = value.at("profile");
    fields(profile, {"ue4", "ue5", "customVersions"});
    result.profile.ue4 = version_range(profile.at("ue4"));
    result.profile.ue5 = version_range(profile.at("ue5"));
    if (profile.contains("customVersions")) {
        require(profile["customVersions"].is_object(), "Expected custom version intervals");
        for (auto it = profile["customVersions"].begin(); it != profile["customVersions"].end(); ++it)
            result.profile.custom_versions.emplace(it.key(), version_range(it.value()));
    }
    return result;
}
} // namespace
struct BinaryLayouts::Impl { Json document; std::string hash; };
BinaryLayouts::BinaryLayouts(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
BinaryLayouts BinaryLayouts::parse(const Json &document, const BinaryLayoutOptions &options) {
    try {
        DefinitionValidator{options}.json(document);
        fields(document, {"format", "layouts", "decoders"});
        require(document.at("format") == "cyuediff.binary-layouts/1", "Unsupported binary layout format");
        const auto &layouts = document.at("layouts");
        require(layouts.is_object(), "Expected named binary layouts");
        std::map<std::string, std::set<std::string>> edges;
        for (auto it = layouts.begin(); it != layouts.end(); ++it) {
            options.control.check(); word(it.key()); node(it.value(), edges[it.key()]);
        }
        std::map<std::string, unsigned> depths;
        std::set<std::string> visiting;
        std::function<unsigned(const std::string &)> visit = [&](const std::string &key) {
            options.control.check();
            require(edges.count(key) != 0, "Unknown referenced binary layout");
            auto known = depths.find(key);
            if (known != depths.end()) return known->second;
            require(visiting.insert(key).second, "Cyclic binary layout references");
            if (visiting.size() >= options.max_depth)
                throw Error("layout-limit", "Layout reference nesting exceeds limit");
            unsigned depth = 1;
            for (const auto &child : edges.at(key)) depth = std::max(depth, 1 + visit(child));
            visiting.erase(key);
            if (depth >= options.max_depth) throw Error("layout-limit", "Layout reference nesting exceeds limit");
            depths.emplace(key, depth); return depth;
        };
        for (const auto &entry : edges) visit(entry.first);
        std::vector<AssetDecoder> registrations;
        if (document.contains("decoders")) {
            require(document["decoders"].is_array(), "Expected layout decoder array");
            for (const auto &value : document["decoders"]) {
                options.control.check();
                require(layouts.contains(word(value.at("layout"))), "Unknown decoder binary layout");
                auto entry = binding(value);
                entry.decode = [](DecoderInput &, const DecoderContext &) { return Json(nullptr); };
                registrations.push_back(std::move(entry));
            }
        }
        DecoderRegistry validate(std::move(registrations));
        auto text = document.dump();
        if (text.size() > options.max_bytes) throw Error("layout-limit", "Layout definition exceeds byte limit");
        auto impl = std::make_shared<Impl>();
        impl->document = document;
        impl->hash = sha256_bytes(std::vector<std::uint8_t>(text.begin(), text.end()), options.control);
        return BinaryLayouts(std::move(impl));
    } catch (const Json::exception &) { throw Error("layout-format", "Invalid or missing binary layout field"); }
}
BinaryLayouts BinaryLayouts::parse_text(const std::string &text, const BinaryLayoutOptions &options) {
    if (text.size() > options.max_bytes) throw Error("layout-limit", "Layout definition exceeds byte limit");
    try {
        std::vector<std::set<std::string>> keys;
        auto document = Json::parse(text, [&](int depth, Json::parse_event_t event, Json &value) {
            options.control.check();
            if (depth < 0 || static_cast<unsigned>(depth) >= options.max_depth)
                throw Error("layout-limit", "Layout JSON nesting exceeds limit");
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            if (event == Json::parse_event_t::key)
                require(keys.back().insert(value.get<std::string>()).second, "Duplicate layout JSON key");
            if (event == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        });
        return parse(document, options);
    } catch (const Json::exception &) { throw Error("layout-format", "Malformed binary layout JSON"); }
}
BinaryLayouts BinaryLayouts::read(const fs::path &path, const BinaryLayoutOptions &options) {
    options.control.check();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw Error("file-open", "Cannot open binary layouts: " + path.u8string());
    auto size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > options.max_bytes ||
        static_cast<std::uint64_t>(size) > std::string{}.max_size())
        throw Error("layout-limit", "Layout definition exceeds byte limit");
    std::string text(static_cast<std::size_t>(size), '\0'); input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size())) || input.peek() != EOF)
        throw Error("file-read", "Cannot read stable binary layout bytes");
    return parse_text(text, options);
}
bool BinaryLayouts::contains(const std::string &name) const { return impl_->document["layouts"].contains(name); }
const std::string &BinaryLayouts::sha256() const { return impl_->hash; }
Json BinaryLayouts::to_json() const { return impl_->document; }
namespace {
struct LayoutReader {
    DecoderInput &input;
    const Json &layouts;
    const BinaryLayoutReadOptions &options;
    std::uint32_t values = 0;
    std::uint64_t text_bytes = 0, begin = input.position();
    void work(unsigned depth) {
        options.control.check();
        if (depth >= options.max_depth || values >= options.max_values)
            throw Error("layout-limit", "Layout values exceed node/depth limit", input.position());
        if (input.position() < begin || input.position() - begin > options.max_read_bytes)
            throw Error("layout-limit", "Layout input exceeds byte limit", input.position());
        if (options.charge) options.charge(1, 0);
        ++values;
    }
    void bytes(std::uint64_t size) {
        const auto at = input.position();
        if (at < begin || at - begin > options.max_read_bytes || size > options.max_read_bytes - (at - begin))
            throw Error("layout-limit", "Layout input exceeds byte limit", at);
        if (size > input.remaining()) throw Error("bounds", "Truncated binary layout value", at);
    }
    void text(const std::string &value) {
        if (value.size() > options.max_string_bytes || value.size() > options.max_text_bytes - text_bytes)
            throw Error("layout-limit", "Layout text exceeds limit", input.position());
        detail::validate_byte_string(value, false, options.control, "layout-value", input.position());
        if (options.charge) options.charge(0, value.size());
        text_bytes += value.size();
    }
    template <class T> T get() { bytes(sizeof(T)); return input.get<T>(); }
    std::uint32_t count(const Json &value) {
        if (value.is_number_integer()) return value.get<std::uint32_t>();
        auto result = get<std::int32_t>();
        if (result < 0) throw Error("layout-value", "Negative binary layout count", input.position() - 4);
        return static_cast<std::uint32_t>(result);
    }
    std::string string() {
        const auto length = get<std::int32_t>();
        if (!length) return {};
        const auto count = length < 0 ? std::uint64_t(-std::int64_t(length)) : std::uint64_t(length);
        const auto size = count * (length < 0 ? 2 : 1);
        bytes(size);
        if (size > options.max_string_bytes || size > options.max_text_bytes - text_bytes)
            throw Error("layout-limit", "Layout FString exceeds limit", input.position());
        std::string result;
        auto append = [&](std::uint32_t cp) {
            if (cp < 0x80) result += char(cp);
            else if (cp < 0x800) { result += char(0xc0 | (cp >> 6)); result += char(0x80 | (cp & 63)); }
            else if (cp < 0x10000) { result += char(0xe0 | (cp >> 12)); result += char(0x80 | ((cp >> 6) & 63)); result += char(0x80 | (cp & 63)); }
            else { result += char(0xf0 | (cp >> 18)); result += char(0x80 | ((cp >> 12) & 63)); result += char(0x80 | ((cp >> 6) & 63)); result += char(0x80 | (cp & 63)); }
            if (result.size() > options.max_string_bytes || result.size() > options.max_text_bytes - text_bytes)
                throw Error("layout-limit", "Decoded layout FString exceeds limit", input.position());
        };
        for (std::uint64_t i = 0; i < count; ++i) {
            if ((i & 4095) == 0) options.control.check();
            auto cp = length < 0 ? std::uint32_t(get<std::uint16_t>()) : std::uint32_t(get<std::uint8_t>());
            if (i + 1 == count) {
                if (cp) throw Error("layout-value", "Unterminated layout FString", input.position());
                break;
            }
            if (length > 0 && cp >= 128)
                throw Error("layout-value", "Non-ASCII layout FString requires an explicit encoding", input.position());
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (i + 2 >= count) throw Error("layout-value", "Truncated UTF-16 surrogate", input.position());
                const auto low = get<std::uint16_t>(); ++i;
                if (low < 0xdc00 || low > 0xdfff) throw Error("layout-value", "Invalid UTF-16 surrogate", input.position());
                cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
            } else if (cp >= 0xdc00 && cp <= 0xdfff)
                throw Error("layout-value", "Unpaired UTF-16 surrogate", input.position());
            append(cp);
        }
        return result;
    }
    Json read(const Json &node, unsigned depth) {
        work(depth);
        const auto type = node["type"].get<std::string>();
        if (type == "record") {
            Json value = Json::object();
            for (const auto &field : node["fields"]) {
                const auto &name = field["name"].get_ref<const std::string &>(); text(name);
                value[name] = read(field["value"], depth + 1);
            }
            return value;
        }
        if (type == "ref") return read(layouts.at(node["layout"].get<std::string>()), depth + 1);
        if (type == "array" || type == "bytes") {
            auto size = count(node["count"]);
            if (type == "bytes") {
                bytes(size);
                if (std::uint64_t(size) * 2 > options.max_string_bytes ||
                    std::uint64_t(size) * 2 > options.max_text_bytes - text_bytes)
                    throw Error("layout-limit", "Layout byte string exceeds text limit", input.position());
                std::string result; result.reserve(static_cast<std::size_t>(size) * 2);
                constexpr char hex[] = "0123456789abcdef";
                for (std::uint32_t i = 0; i < size; ++i) {
                    if ((i & 4095) == 0) options.control.check();
                    const auto byte = input.get<std::uint8_t>();
                    result += hex[byte >> 4]; result += hex[byte & 15];
                }
                text(result); return result;
            }
            if (size > options.max_values - values)
                throw Error("layout-limit", "Layout array count exceeds work limit", input.position());
            Json result = Json::array();
            for (std::uint32_t i = 0; i < size; ++i) result.push_back(read(node["element"], depth + 1));
            return result;
        }
        if (type == "optional") {
            const auto present = node["presence"] == "bool8" ? get<std::uint8_t>() : get<std::uint32_t>();
            if (present > 1) throw Error("layout-value", "Invalid layout presence marker", input.position());
            Json result = {{"isSet", present != 0}};
            if (present) result["value"] = read(node["value"], depth + 1);
            return result;
        }
        if (type == "u8") return get<std::uint8_t>();
        if (type == "i8") return get<std::int8_t>();
        if (type == "u16") return get<std::uint16_t>();
        if (type == "i16") return get<std::int16_t>();
        if (type == "u32") return get<std::uint32_t>();
        if (type == "i32") return get<std::int32_t>();
        if (type == "u64") return get<std::uint64_t>();
        if (type == "i64") return get<std::int64_t>();
        if (type == "f32" || type == "f64") {
            auto result = type == "f32" ? double(get<float>()) : get<double>();
            if (!std::isfinite(result)) throw Error("layout-value", "Non-finite layout number", input.position());
            return result;
        }
        if (type == "bool8" || type == "bool32") {
            auto result = type == "bool8" ? get<std::uint8_t>() : get<std::uint32_t>();
            if (result > 1) throw Error("layout-value", "Invalid layout boolean", input.position());
            return result != 0;
        }
        bytes(type == "name" ? 8 : type == "guid" ? 16 : 4);
        std::string result = type == "name" ? input.name() : type == "guid" ? input.guid() :
                             type == "object" ? input.reference() : string();
        bytes(0); text(result); return result;
    }
};
} // namespace
Json BinaryLayouts::decode(DecoderInput &input, const std::string &name, const BinaryLayoutReadOptions &options) const {
    options.control.check();
    if (!contains(name)) throw Error("layout-missing", "Unknown binary layout: " + name);
    return LayoutReader{input, impl_->document["layouts"], options}.read(impl_->document["layouts"][name], 0);
}
std::shared_ptr<const DecoderRegistry> BinaryLayouts::decoder_registry(const BinaryLayoutReadOptions &options) const {
    std::vector<AssetDecoder> result;
    if (impl_->document.contains("decoders"))
        for (const auto &value : impl_->document["decoders"]) {
            options.control.check();
            auto entry = binding(value); auto name = value["layout"].get<std::string>();
            entry.decode = [catalog = *this, name, options](DecoderInput &input, const DecoderContext &) {
                return catalog.decode(input, name, options);
            };
            result.push_back(std::move(entry));
        }
    return std::make_shared<const DecoderRegistry>(std::move(result));
}
} // namespace cyuediff
